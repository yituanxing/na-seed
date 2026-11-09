#!/usr/bin/env python3
"""One physical QEMU original-Alpine poweroff, not a host-terminated login probe.

The unchanged Alpine /sbin/init runs OpenRC and its real BusyBox poweroff.
The kernel may report PASS only after PID1 is dead and lwext4 has durably
unmounted.  We additionally require the QEMU *runner itself* to exit normally
and the raw image to release its write lock.  Disk fsck/reopen is a separate
acceptance step, not something this UART driver may claim to have performed.
"""

import importlib.util
import os
from pathlib import Path
import re
import subprocess
import sys
import time


def load_sibling(name: str, module: str):
    spec = importlib.util.spec_from_file_location(module, Path(__file__).with_name(name))
    assert spec is not None and spec.loader is not None
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


m193 = load_sibling("m193-alpine-uart.py", "naseed_m241_m193")
m194 = load_sibling("m194-alpine-uart.py", "naseed_m241_m194")

NONCE = "M241_REAL_EXT4_POWEROFF_20261109"
COMMANDS = (
    ("printf '" + NONCE + "\\n' > /root/m241-poweroff-nonce.txt; "
     "printf 'M241_EXT4_WRITE_RC=%d\\n' \"$?\"\r",
     "M241_EXT4_WRITE_RC=0", "real Alpine ash ext4 write"),
    ("sync; printf 'M241_SYNC_RC=%d\\n' \"$?\"\r",
     "M241_SYNC_RC=0", "real guest sync"),
    ("v=$(cat /root/m241-poweroff-nonce.txt); "
     "printf 'M241_NONCE_READBACK=%s\\n' \"$v\"\r",
     "M241_NONCE_READBACK=" + NONCE, "guest physical ext4 nonce readback"),
)
REQUIRED_ORDER = (
    "[M191 ROOT] physical Alpine ext4 registered at /",
    "[M191 ROOT] original Alpine PID1 from mounted ext4: /sbin/init",
    "OpenRC 0.63 is starting up Linux 0.1",
    "[EXEC] replaced process with lwext4 ELF: /sbin/getty",
    "[EXEC] replaced process with lwext4 ELF: /bin/login",
    "M241_EXT4_WRITE_RC=0",
    "M241_SYNC_RC=0",
    "M241_NONCE_READBACK=" + NONCE,
    "[EXEC] replaced process with lwext4 ELF: /sbin/poweroff",
    "[EXIT] user status=0",
    "[M191 PID1 QUIESCED] no live non-init user Tasks",
    "[LWEXT4 GUEST] clean upstream unmount and durable flush",
    "[ALPINE POWER_OFF] original BusyBox; durable ext4 unmount",
    "[PASS] cmdline userspace exit 0",
)


def verify_shutdown_uart(text: str) -> None:
    """Require real, ordered kernel/guest evidence, never an echoed command."""
    if "[FAIL]" in text or any(marker in text for marker in m193.OPENRC_FATAL):
        raise RuntimeError("native guest reported boot/shutdown failure")
    pos = -1
    for marker in REQUIRED_ORDER:
        next_pos = text.find(marker, pos + 1)
        if next_pos < 0:
            raise RuntimeError("missing or out-of-order physical UART marker: " + marker)
        pos = next_pos
    if "[EXEC] replaced process with lwext4 ELF: /bin/sh" not in text and (
        "[EXEC] replaced process with lwext4 ELF: /bin/ash" not in text
    ):
        raise RuntimeError("original /bin/sh or /bin/ash was never executed")


def run(runner: str, serial: Path, runner_log: Path, fifo: Path,
        deadline: int) -> None:
    fifo.parent.mkdir(parents=True, exist_ok=True)
    if fifo.exists():
        raise RuntimeError("refusing to reuse stale UART FIFO: " + str(fifo))
    if serial.exists():
        serial.unlink()  # No stale serial PASS from an earlier boot.
    os.mkfifo(fifo, 0o600)
    hold = os.open(fifo, os.O_RDWR | os.O_NONBLOCK)
    runner_log.parent.mkdir(parents=True, exist_ok=True)
    proc = None
    observed = bytearray()
    state = "login"
    step = 0
    step_start = 0
    marker_end = 0
    shell_at = 0.0
    dsr_replies = 0
    authenticated_getty = False
    started = time.monotonic()
    verified = False
    try:
        with runner_log.open("wb") as runner_output:
            proc = subprocess.Popen(["sh", runner], stdout=runner_output,
                                    stderr=subprocess.STDOUT, start_new_session=True)
            cursor = 0
            while time.monotonic() - started < deadline:
                if serial.is_file():
                    with serial.open("rb") as stream:
                        stream.seek(cursor)
                        chunk = stream.read()
                        cursor += len(chunk)
                    observed.extend(chunk)
                text = observed.decode("utf-8", errors="replace").replace("\r", "")
                if ("[FAIL]" in text or "TIOCSCTTY: Operation not permitted" in text
                        or any(marker in text for marker in m193.OPENRC_FATAL)):
                    raise RuntimeError("original Alpine reported boot/tty/shutdown failure")

                # The original BusyBox ash queries VT100 cursor position.
                while dsr_replies < text.count("\x1b[6n"):
                    m193.sent(hold, "\x1b[1;1R")
                    dsr_replies += 1

                if state == "login" and (
                    m194.getty_login_host(text, ("naseed", "localhost")) is not None
                ):
                    print("[M241 UART] physical original Alpine getty; logging in as root", flush=True)
                    authenticated_getty = True
                    m193.sent(hold, "root\r")
                    state = "password"
                elif state == "password" and "Password:" in text:
                    m193.sent(hold, m193.PASSWORD + "\r")
                    state = "shell"
                elif state == "shell" and (
                    "[EXEC] replaced process with lwext4 ELF: /bin/sh" in text or
                    "[EXEC] replaced process with lwext4 ELF: /bin/ash" in text or
                    m193.shell_prompt(text)
                ):
                    if not shell_at:
                        shell_at = time.monotonic()
                    if time.monotonic() - shell_at >= 0.6:
                        print("[M241 UART] authenticated original shell; writing ext4 nonce", flush=True)
                        step_start = len(text)
                        m193.sent(hold, COMMANDS[0][0])
                        state = "commands"
                elif state == "commands":
                    _cmd, expected, description = COMMANDS[step]
                    segment = text[step_start:]
                    match = re.search(r"(?:^|\n)" + re.escape(expected) + r"(?:\n|$)", segment)
                    if match:
                        print(f"[M241 UART] verified {description}", flush=True)
                        marker_end = step_start + match.end()
                        step += 1
                        state = "await_prompt"
                    elif m194.shell_prompt_after_step(segment):
                        raise RuntimeError("original shell command returned without required output: " + description)
                elif state == "await_prompt" and m194.shell_prompt_after_step(text[marker_end:]):
                    step_start = len(text)
                    if step < len(COMMANDS):
                        m193.sent(hold, COMMANDS[step][0])
                        state = "commands"
                    else:
                        if not authenticated_getty:
                            raise RuntimeError("never authenticated through genuine getty")
                        print("[M241 UART] issuing ORIGINAL /sbin/poweroff (not QMP quit)", flush=True)
                        m193.sent(hold, "/sbin/poweroff\r")
                        state = "shutdown"

                if proc.poll() is not None:
                    if state != "shutdown":
                        raise RuntimeError(f"native QEMU runner exited prematurely: rc={proc.returncode}, state={state}")
                    # A successful kernel debug exit is QEMU rc=1, converted
                    # by the established native parity shell to runner rc=0.
                    if proc.returncode != 0:
                        raise RuntimeError(f"native QEMU runner failed/timeout: rc={proc.returncode}")
                    # The shell runner owns stdout redirection; after it
                    # terminates the serial file is stable. Read the final
                    # bytes now rather than racing a pre-poll stream read.
                    if serial.is_file():
                        final = serial.read_bytes()
                        observed = bytearray(final)
                        text = final.decode("utf-8", errors="replace").replace("\r", "")
                    verify_shutdown_uart(text)
                    if not os.environ.get("NAS_VIRTIO_BLK_IMAGE"):
                        raise RuntimeError("missing exact physical ext4 image for QEMU-exit lock check")
                    if not m194.disk_lock_released(os.environ["NAS_VIRTIO_BLK_IMAGE"]):
                        raise RuntimeError("QEMU runner ended without releasing its ext4 disk lock")
                    if m194.image_qemu_pids(os.environ["NAS_VIRTIO_BLK_IMAGE"]):
                        raise RuntimeError("physical QEMU still has original ext4 image open")
                    verified = True
                    print("[M241 ORIGINAL ALPINE NATIVE POWEROFF UART PASS] physical getty/password "
                          "-> original /sbin/poweroff -> PID1 quiesce -> durable lwext4 "
                          "unmount -> QEMU natural exit -> exclusive disk lock released; "
                          "read-only fsck and independent second boot remain separate", flush=True)
                    return
                time.sleep(0.10)
            raise RuntimeError(f"M241 physical shutdown deadline exceeded at state={state}, step={step}")
    except Exception:
        print("[M241 ORIGINAL ALPINE NATIVE POWEROFF FAIL] physical serial tail:", file=sys.stderr)
        print(observed.decode("utf-8", errors="replace")[-14000:], file=sys.stderr)
        raise
    finally:
        try:
            if proc is not None and not verified:
                # Failure cleanup only. Never turn a host SIGTERM into PASS.
                m194.stop_runner(proc)
        finally:
            os.close(hold)
            fifo.unlink()
            if observed:
                serial.with_name(serial.name.removesuffix(".serial.log") + ".clean.log").write_bytes(
                    bytes(observed).replace(b"\r", b""))


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit("usage: m241-alpine-poweroff-uart.py RUNNER SERIAL_LOG RUNNER_LOG FIFO")
    seconds = int(os.environ.get("NAS_M241_DIAG_SECONDS", "180"))
    run(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]), seconds + 35)


if __name__ == "__main__":
    main()
