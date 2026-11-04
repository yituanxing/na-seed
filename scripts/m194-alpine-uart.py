#!/usr/bin/env python3
"""Exercise original Alpine APK, OpenRC and ext4 through a real QEMU UART.

The M193 password-authenticated shell is the prerequisite, not a replacement.
Every M194 step waits for a new *guest output line*, never a command echo,
host-inferred return status, timer expiration or synthesized shell response.
"""

import importlib.util
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time


_m193_spec = importlib.util.spec_from_file_location(
    "naseed_m193_uart", Path(__file__).with_name("m193-alpine-uart.py"))
assert _m193_spec is not None and _m193_spec.loader is not None
m193 = importlib.util.module_from_spec(_m193_spec)
_m193_spec.loader.exec_module(m193)


# Commands are sent to upstream BusyBox ash after upstream getty/login has
# authenticated a real user.  The status marker is deliberately different
# from the text of the echoed shell command: output_line() requires the
# expected value on an entire line.  No guest test script or native ELF is
# added to the official distribution rootfs.
COMMANDS = (
    ("apk --version; printf 'M194_APK_ELF_RC=%d\\n' \"$?\"\r",
     "M194_APK_ELF_RC=0", "original apk executable"),
    ("apk info -e alpine-base && apk info -e openrc && apk info -e busybox-openrc; "
     "printf 'M194_APK_DB_RC=%d\\n' \"$?\"\r",
     "M194_APK_DB_RC=0", "signed distro package database"),
    ("rc-status; printf 'M194_RC_STATUS_RC=%d\\n' \"$?\"\r",
     "M194_RC_STATUS_RC=0", "original OpenRC service status"),
    ("test -x /etc/init.d/crond; printf 'M194_CROND_SCRIPT_RC=%d\\n' \"$?\"\r",
     "M194_CROND_SCRIPT_RC=0", "original crond OpenRC script"),
    ("rc-service crond start; printf 'M194_CROND_START_RC=%d\\n' \"$?\"\r",
     "M194_CROND_START_RC=0", "real OpenRC crond start"),
    ("rc-service crond status; printf 'M194_CROND_STATUS_RC=%d\\n' \"$?\"\r",
     "M194_CROND_STATUS_RC=0", "real OpenRC crond running status"),
    ("p=\"$(pidof crond)\"; test -n \"$p\" && "
     "printf 'M194_CROND_PID=%s\\n' \"$p\"\r",
     re.compile(r"(?:^|\n)M194_CROND_PID=([0-9]+(?: [0-9]+)*)(?:\n|$)"),
     "genuine crond process PID"),
    ("rc-service crond stop; printf 'M194_CROND_STOP_RC=%d\\n' \"$?\"\r",
     "M194_CROND_STOP_RC=0", "real OpenRC crond stop"),
    ("rc-service crond status >/dev/null 2>&1; rc=$?; "
     "[ \"$rc\" -ne 0 ] && printf 'M194_CROND_STOPPED\\n'\r",
     "M194_CROND_STOPPED", "crond stopped status"),
    ("pidof crond >/dev/null; rc=$?; "
     "[ \"$rc\" -ne 0 ] && printf 'M194_CROND_NO_PID\\n'\r",
     "M194_CROND_NO_PID", "crond process absent"),
    ("printf 'M194_DURABLE_REAL_EXT4_2026\\n' > /root/m194-persistent.txt; "
     "printf 'M194_EXT4_WRITE_RC=%d\\n' \"$?\"\r",
     "M194_EXT4_WRITE_RC=0", "original ash writable ext4 file"),
    ("sync; printf 'M194_SYNC_RC=%d\\n' \"$?\"\r",
     "M194_SYNC_RC=0", "guest sync completed"),
    ("cat /root/m194-persistent.txt\r",
     "M194_DURABLE_REAL_EXT4_2026", "guest ext4 readback of written bytes"),
)


def step_match(segment: str, expected: str | re.Pattern[str]) -> re.Match[str] | None:
    if isinstance(expected, str):
        return re.search(r"(?:^|\n)" + re.escape(expected) + r"(?:\n|$)", segment)
    return expected.search(segment)


def step_observed(segment: str, expected: str | re.Pattern[str]) -> bool:
    return step_match(segment, expected) is not None


def run(runner: str, log_name: Path, runner_log: Path, fifo: Path, deadline: int) -> None:
    fifo.parent.mkdir(parents=True, exist_ok=True)
    if fifo.exists():
        raise RuntimeError("refusing to reuse an existing serial FIFO: " + str(fifo))
    if log_name.exists():
        log_name.unlink()  # Never accept stale UART output from an older run.
    os.mkfifo(fifo, 0o600)
    hold = os.open(fifo, os.O_RDWR | os.O_NONBLOCK)
    runner_log.parent.mkdir(parents=True, exist_ok=True)
    proc = None
    observed = bytearray()
    state = "login"
    step = 0
    step_start = 0
    step_end = 0
    shell_at = 0.0
    dsr_replies = 0
    started = time.monotonic()
    try:
        with runner_log.open("wb") as runner_out:
            proc = subprocess.Popen(
                ["sh", runner], stdout=runner_out,
                stderr=subprocess.STDOUT, start_new_session=True)
            cursor = 0
            while time.monotonic() - started < deadline:
                if log_name.is_file():
                    with log_name.open("rb") as stream:
                        stream.seek(cursor)
                        chunk = stream.read()
                        cursor += len(chunk)
                    observed.extend(chunk)
                text = observed.decode("utf-8", errors="replace").replace("\r", "")
                if ("[FAIL]" in text or "TIOCSCTTY: Operation not permitted" in text
                        or any(marker in text for marker in m193.OPENRC_FATAL)):
                    raise RuntimeError("original Alpine reported a boot or tty failure")

                # Genuine upstream BusyBox ash waits for a VT100 cursor
                # position response before displaying its next prompt.
                while dsr_replies < text.count("\x1b[6n"):
                    m193.sent(hold, "\x1b[1;1R")
                    dsr_replies += 1

                if state == "login" and "naseed login:" in text:
                    print("[M194 UART] original getty; submitting root", flush=True)
                    m193.sent(hold, "root\r")
                    state = "password"
                elif state == "password" and "Password:" in text:
                    print("[M194 UART] original login requested password", flush=True)
                    m193.sent(hold, m193.PASSWORD + "\r")
                    state = "shell"
                elif state == "shell" and (
                    "[EXEC] replaced process with lwext4 ELF: /bin/sh" in text or
                    "[EXEC] replaced process with lwext4 ELF: /bin/ash" in text or
                    m193.shell_prompt(text)
                ):
                    if shell_at == 0.0:
                        shell_at = time.monotonic()
                    if time.monotonic() - shell_at >= 0.6:
                        print("[M194 UART] authenticated original ash; probing services", flush=True)
                        step_start = len(text)
                        m193.sent(hold, COMMANDS[0][0])
                        state = "commands"
                elif state == "commands":
                    _command, expected, description = COMMANDS[step]
                    segment = text[step_start:]
                    found = step_match(segment, expected)
                    if found is not None:
                        print(f"[M194 UART] verified {description}", flush=True)
                        step_end = step_start + found.end()
                        step += 1
                        if step == len(COMMANDS):
                            required = m193.PROOF + (
                                "[EXEC] replaced process with lwext4 ELF: /bin/login",
                            )
                            missing = [marker for marker in required if marker not in text]
                            if missing:
                                raise RuntimeError("missing physical QEMU login proof: "
                                                   + ", ".join(missing))
                            print("[M194 ALPINE SERVICES PASS] original ext4 Alpine PID1 "
                                  "-> OpenRC -> password login -> genuine apk database "
                                  "-> rc-service crond start/status/pid/stop "
                                  "-> guest ext4 write/sync/readback; "
                                  "timeout was NOT a PASS", flush=True)
                            return
                        # Let upstream ash complete its read/execute/print
                        # cycle.  Its VT100 prompt may arrive later than the
                        # printf marker, especially around OpenRC daemon
                        # startup.  Queueing the next command too early can
                        # interleave with a cursor-position report.
                        state = "await_prompt"
                    elif m193.shell_prompt(segment):
                        # A new REAL ash prompt means this command already
                        # returned.  Stop immediately when apk/rc-service or
                        # the daemon check failed; do not wait out the entire
                        # QEMU budget and conceal the original exit status.
                        observed_rc = re.findall(r"M194_[A-Z_]+_RC=[0-9]+", segment)
                        raise RuntimeError(
                            f"original Alpine command {step + 1} ({description}) "
                            f"completed without its required result; guest_rc={observed_rc[-1:]}")
                elif state == "await_prompt" and m193.shell_prompt(text[step_end:]):
                    step_start = len(text)
                    m193.sent(hold, COMMANDS[step][0])
                    state = "commands"

                if proc.poll() is not None:
                    raise RuntimeError(f"native QEMU runner exited before proof: {proc.returncode}")
                time.sleep(0.10)
            raise RuntimeError(f"M194 original Alpine proof timed out: state={state} step={step}")
    except Exception:
        print("[M194 ALPINE SERVICES FAIL] raw QEMU UART tail:", file=sys.stderr)
        print(observed.decode("utf-8", errors="replace")[-13000:], file=sys.stderr)
        raise
    finally:
        if proc is not None and proc.poll() is None:
            try:
                os.killpg(proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                proc.wait(timeout=8)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(proc.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                proc.wait()
        os.close(hold)
        fifo.unlink()
        if observed:
            clean_name = log_name.with_name(
                log_name.name.removesuffix(".serial.log") + ".clean.log")
            clean_name.write_bytes(bytes(observed).replace(b"\r", b""))


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit("usage: m194-alpine-uart.py RUNNER SERIAL_LOG RUNNER_LOG FIFO")
    seconds = int(os.environ.get("NAS_M194_DIAG_SECONDS", "150"))
    run(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]), seconds + 35)


if __name__ == "__main__":
    main()
