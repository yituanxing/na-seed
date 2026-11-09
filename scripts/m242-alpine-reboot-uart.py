#!/usr/bin/env python3
"""One QEMU PID, two actual Alpine boots, durable reboot and final poweroff.

The CI runner supplies the original signed Alpine physical ext4 and native
QEMU. We authenticate over a real UART on boot 1, write a unique guest nonce,
run upstream /sbin/reboot, witness firmware reset and a SECOND Alpine PID1,
authenticate again, read the same persistent bytes, and finally run upstream
/sbin/poweroff. A new QEMU process, old echoed UART, timeout or host SIGTERM
is never reboot/poweroff success. Read-only disk fsck is a separate host step.
"""

import importlib.util
import os
from pathlib import Path
import re
import subprocess
import sys
import time


def sibling(filename: str, modulename: str):
    spec = importlib.util.spec_from_file_location(modulename, Path(__file__).with_name(filename))
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


m193 = sibling("m193-alpine-uart.py", "naseed_m242_m193")
m194 = sibling("m194-alpine-uart.py", "naseed_m242_m194")

NONCE = "M242_REAL_EXT4_REBOOT_SAME_QEMU_20261109"
FILE = "/root/m242-reboot-nonce.txt"
RESET_MARKER = "[M242 RESET] real platform reset"
FIRST_COMMANDS = (
    ("printf '" + NONCE + "\\n' > " + FILE + "; "
     "printf 'M242_WRITE_RC=%d\\n' \"$?\"\r",
     "M242_WRITE_RC=0", "first-boot physical ext4 write"),
    ("sync; printf 'M242_SYNC_RC=%d\\n' \"$?\"\r",
     "M242_SYNC_RC=0", "first-boot guest sync"),
)
SECOND_COMMANDS = (
    ("v=$(cat " + FILE + "); "
     "printf 'M242_SECOND_BOOT_NONCE=%s\\n' \"$v\"\r",
     "M242_SECOND_BOOT_NONCE=" + NONCE,
     "second-boot readback of physical ext4 nonce"),
)
BOOT = (
    "[M191 ROOT] physical Alpine ext4 registered at /",
    "[M191 ROOT] original Alpine PID1 from mounted ext4: /sbin/init",
    "OpenRC 0.63 is starting up Linux 0.1",
    "[EXEC] replaced process with lwext4 ELF: /sbin/getty",
    "[EXEC] replaced process with lwext4 ELF: /bin/login",
)
FLUSH = (
    "[EXIT] user status=0",
    "[M191 PID1 QUIESCED] no live non-init user Tasks",
    "[LWEXT4 GUEST] clean upstream unmount and durable flush",
)


def reboot_marker() -> str:
    marker = os.environ.get("NAS_M242_RESET_MARKER", RESET_MARKER)
    if not marker or "\n" in marker or "\r" in marker:
        raise ValueError("M242 reset marker must be one nonempty physical UART line")
    return marker


def verify_two_boots(text: str, *, reset: str = RESET_MARKER) -> None:
    """Strict cumulative serial order; no status made from echoed shell text."""
    if "[FAIL]" in text or "TIOCSCTTY: Operation not permitted" in text or (
        any(marker in text for marker in m193.OPENRC_FATAL)
    ):
        raise RuntimeError("original Alpine native guest reported failure")
    milestones = (
        *BOOT,
        "M242_WRITE_RC=0",
        "M242_SYNC_RC=0",
        "[EXEC] replaced process with lwext4 ELF: /sbin/reboot",
        *FLUSH,
        reset,
        *BOOT,
        "M242_SECOND_BOOT_NONCE=" + NONCE,
        "[EXEC] replaced process with lwext4 ELF: /sbin/poweroff",
        *FLUSH,
        "[ALPINE POWER_OFF] original BusyBox; durable ext4 unmount",
        "[PASS] cmdline userspace exit 0",
    )
    pos = -1
    for marker in milestones:
        # Kernel diagnostics include bracketed prefixes; the three guest
        # printf acknowledgements MUST occupy their own complete UART line.
        # A broken nonce with the correct string as its prefix is not proof.
        if marker.startswith("M242_"):
            match = re.search(r"(?m)^" + re.escape(marker) + r"$", text[pos + 1:])
            index = pos + 1 + match.start() if match else -1
        else:
            index = text.find(marker, pos + 1)
        if index < 0:
            raise RuntimeError("missing/out-of-order two-boot physical UART marker: " + marker)
        pos = index
    parts = text.split(reset, 1)
    if len(parts) != 2 or any(
        "[EXEC] replaced process with lwext4 ELF: /bin/sh" not in part and
        "[EXEC] replaced process with lwext4 ELF: /bin/ash" not in part
        for part in parts
    ):
        raise RuntimeError("original shell was not executed in BOTH Alpine boots")


def image_qemu_pid(image: str, *, expected: int | None = None) -> int:
    """Identify exactly one real system-QEMU PID owning the SAME raw disk."""
    owners = m194.image_qemu_pids(image)
    if len(owners) != 1:
        raise RuntimeError(f"expected exactly one physical-image QEMU PID, observed {owners}")
    if expected is not None and owners[0] != expected:
        raise RuntimeError(f"QEMU PID changed across reset: old={expected}, new={owners[0]}")
    return owners[0]


def run(runner: str, serial: Path, runner_log: Path, fifo: Path,
        deadline: int) -> None:
    image = os.environ.get("NAS_VIRTIO_BLK_IMAGE")
    if not image or not Path(image).is_file():
        raise RuntimeError("M242 requires one already-existing real physical ext4 image")
    reset = reboot_marker()
    fifo.parent.mkdir(parents=True, exist_ok=True)
    if fifo.exists():
        raise RuntimeError("refusing to reuse a stale serial FIFO: " + str(fifo))
    if serial.exists():
        serial.unlink()  # No previous run may satisfy either boot epoch.
    os.mkfifo(fifo, 0o600)
    hold = os.open(fifo, os.O_RDWR | os.O_NONBLOCK)
    runner_log.parent.mkdir(parents=True, exist_ok=True)
    observed = bytearray()
    proc = None
    verified = False
    state = "login"
    boot = 1
    epoch = 0  # Ignore ALL previous getty, login, shell or nonce bytes.
    step = 0
    step_start = 0
    step_end = 0
    shell_at = 0.0
    password_sent_at = 0
    dsr_replies = 0
    qemu_pid = None
    logins = 0
    started = time.monotonic()
    try:
        with runner_log.open("wb") as runner_output:
            proc = subprocess.Popen(["sh", runner], stdout=runner_output,
                                    stderr=subprocess.STDOUT, start_new_session=True)
            cursor = 0
            while time.monotonic() - started < deadline:
                if serial.is_file():
                    with serial.open("rb") as stream:
                        stream.seek(cursor)
                        new = stream.read()
                        cursor += len(new)
                    observed.extend(new)
                text = observed.decode("utf-8", errors="replace").replace("\r", "")
                if "[FAIL]" in text or "TIOCSCTTY: Operation not permitted" in text or (
                    any(marker in text for marker in m193.OPENRC_FATAL)
                ):
                    raise RuntimeError("real Alpine guest reported failure")

                while dsr_replies < text.count("\x1b[6n"):
                    m193.sent(hold, "\x1b[1;1R")
                    dsr_replies += 1

                segment = text[epoch:]
                if state == "wait_second_boot":
                    # A real reset must restart the original kernel and
                    # original Alpine init, not just reprint a getty prompt.
                    if all(marker in segment for marker in BOOT[:3]):
                        qemu_pid = image_qemu_pid(image, expected=qemu_pid)
                        print(f"[M242 UART] SAME QEMU PID {qemu_pid} booted second original Alpine", flush=True)
                        state = "login"
                elif state == "login" and (
                    m194.getty_login_host(segment, ("naseed", "localhost")) is not None
                ):
                    # On boot 2, the old epoch ends at the genuine reset
                    # marker, so a boot-1 getty can never satisfy this.
                    print(f"[M242 UART] physical Alpine getty login boot={boot}", flush=True)
                    m193.sent(hold, "root\r")
                    state = "password"
                elif state == "password" and "Password:" in segment:
                    m193.sent(hold, m193.PASSWORD + "\r")
                    password_sent_at = len(text)
                    state = "shell"
                elif state == "shell" and m194.shell_prompt_after_step(text[password_sent_at:]):
                    if shell_at == 0:
                        shell_at = time.monotonic()
                    if time.monotonic() - shell_at >= 0.6:
                        qemu_pid = image_qemu_pid(image, expected=qemu_pid)
                        logins += 1
                        print(f"[M242 UART] original authenticated shell boot={boot}; QEMU PID={qemu_pid}", flush=True)
                        commands = FIRST_COMMANDS if boot == 1 else SECOND_COMMANDS
                        step_start = len(text)
                        m193.sent(hold, commands[0][0])
                        state = "commands"
                elif state == "commands":
                    commands = FIRST_COMMANDS if boot == 1 else SECOND_COMMANDS
                    _command, expected, label = commands[step]
                    proof = text[step_start:]
                    match = re.search(r"(?:^|\n)" + re.escape(expected) + r"(?:\n|$)", proof)
                    if match is not None:
                        print(f"[M242 UART] verified {label}", flush=True)
                        step_end = step_start + match.end()
                        step += 1
                        state = "await_prompt"
                    elif m194.shell_prompt_after_step(proof):
                        raise RuntimeError("original Alpine command ended without independent result: " + label)
                elif state == "await_prompt" and m194.shell_prompt_after_step(text[step_end:]):
                    commands = FIRST_COMMANDS if boot == 1 else SECOND_COMMANDS
                    if step < len(commands):
                        step_start = len(text)
                        m193.sent(hold, commands[step][0])
                        state = "commands"
                    elif boot == 1:
                        print("[M242 UART] issuing original /sbin/reboot in first QEMU guest", flush=True)
                        m193.sent(hold, "/sbin/reboot\r")
                        state = "wait_reset"
                    else:
                        print("[M242 UART] issuing original /sbin/poweroff after second-boot persisted nonce", flush=True)
                        m193.sent(hold, "/sbin/poweroff\r")
                        state = "wait_final_poweroff"
                elif state == "wait_reset":
                    # Reboot must complete the old PID1 deferred filesystem
                    # lifecycle BEFORE platform firmware resets the machine.
                    ordered = (
                        "[EXEC] replaced process with lwext4 ELF: /sbin/reboot",
                        *FLUSH,
                        reset,
                    )
                    pos = -1
                    for marker in ordered:
                        pos = segment.find(marker, pos + 1)
                        if pos < 0:
                            break
                    else:
                        qemu_pid = image_qemu_pid(image, expected=qemu_pid)
                        epoch += pos + len(reset)
                        boot = 2
                        state = "wait_second_boot"
                        step = 0
                        shell_at = 0.0
                        password_sent_at = 0
                        print("[M242 UART] original reboot completed clean ext4 flush; waiting for physical second boot", flush=True)

                if proc.poll() is not None:
                    if state != "wait_final_poweroff":
                        raise RuntimeError(f"QEMU runner exited before SECOND Alpine poweroff: rc={proc.returncode}, boot={boot}, state={state}")
                    if proc.returncode != 0:
                        raise RuntimeError(f"native QEMU runner failed/timeout after second boot: rc={proc.returncode}")
                    if serial.is_file():
                        complete = serial.read_bytes()
                        observed = bytearray(complete)
                        text = complete.decode("utf-8", errors="replace").replace("\r", "")
                    verify_two_boots(text, reset=reset)
                    if logins != 2 or boot != 2 or qemu_pid is None:
                        raise RuntimeError(f"missing two distinct authenticated boots: logins={logins}, boot={boot}")
                    if not m194.disk_lock_released(image) or m194.image_qemu_pids(image):
                        raise RuntimeError("same physical ext4 QEMU image remains locked/open after final poweroff")
                    verified = True
                    print("[M242 ORIGINAL ALPINE TWO BOOTS PASS] SAME native QEMU PID: "
                          "first original Alpine login + ext4 nonce -> original /sbin/reboot "
                          "-> clean filesystem flush -> real platform reset -> SECOND original "
                          "Alpine/OpenRC/getty/login -> persisted nonce -> original /sbin/poweroff "
                          "-> final ext4 flush -> QEMU natural exit/disk lock release; "
                          "independent host fsck remains required", flush=True)
                    return
                time.sleep(0.10)
            raise RuntimeError(f"M242 native QEMU two-boot timeout at boot={boot}, state={state}, step={step}")
    except Exception:
        print("[M242 ORIGINAL ALPINE TWO BOOTS FAIL] physical UART tail:", file=sys.stderr)
        print(observed.decode("utf-8", errors="replace")[-16000:], file=sys.stderr)
        raise
    finally:
        try:
            if proc is not None and not verified:
                m194.stop_runner(proc)  # Failure cleanup NEVER passes acceptance.
        finally:
            os.close(hold)
            fifo.unlink()
            if observed:
                serial.with_name(serial.name.removesuffix(".serial.log") + ".clean.log").write_bytes(
                    bytes(observed).replace(b"\r", b""))


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit("usage: m242-alpine-reboot-uart.py RUNNER SERIAL_LOG RUNNER_LOG FIFO")
    seconds = int(os.environ.get("NAS_M242_DIAG_SECONDS", "360"))
    run(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]), seconds + 35)


if __name__ == "__main__":
    main()
