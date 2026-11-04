#!/usr/bin/env python3
"""Drive real system-QEMU serial RX through Alpine getty/login/ash/sh.

Read the live physical UART log.  The historical parity runner intentionally
fails for a persistent PID1 and only creates its normalized log on exit.
Neither that expected failure nor a timeout is ever a login success oracle.
"""

import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time


PASSWORD = "naseed-m193-only"
COMMANDS = (
    ("id -u\r", "0", "root UID"),
    ("uname -s\r", "Linux", "real uname"),
    ("cat /etc/alpine-release\r", "3.23.7", "official Alpine release"),
    ("exec /bin/sh\r", None, "exec original /bin/sh"),
    ("printf 'M193_POSIX_SH_CONFIRMED\\n'\r", "M193_POSIX_SH_CONFIRMED", "original POSIX shell"),
)
PROOF = (
    "[M191 ROOT] physical Alpine ext4 registered at /",
    "[M191 ROOT] original Alpine PID1 from mounted ext4: /sbin/init",
    "OpenRC 0.63 is starting up Linux 0.1",
    "[EXEC] replaced process with lwext4 ELF: /sbin/getty",
    "naseed login:",
)


def sent(fd: int, command: str) -> None:
    for byte in command.encode("ascii"):
        os.write(fd, bytes((byte,)))
        time.sleep(0.018)


def output_line(segment: str, value: str) -> bool:
    # Match guest output as its OWN line, never the echoed shell command.
    return re.search(r"(?:^|\n)" + re.escape(value) + r"(?:\n|$)", segment) is not None


def shell_prompt(text: str) -> bool:
    # A genuine root ash prompt typically ends in "naseed:~# ".  Accept
    # configured PS1 variations only AFTER real password authentication.
    tail = text[-250:].split("\n")[-1]
    return bool(re.search(r"(?:^|[:~/])[^\n]{0,55}# ?$", tail))


def run(runner: str, log_name: Path, runner_log: Path, fifo: Path, deadline: int) -> None:
    fifo.parent.mkdir(parents=True, exist_ok=True)
    if fifo.exists():
        raise RuntimeError("serial FIFO already exists; refusing to reuse " + str(fifo))
    # Do not accept a stale successful boot from a previous attempt.
    if log_name.exists():
        log_name.unlink()
    os.mkfifo(fifo, 0o600)
    # Linux FIFO O_RDWR holds both ends open before the runner's < redirection.
    # This avoids an otherwise fatal startup deadlock on all three ISA runners.
    hold = os.open(fifo, os.O_RDWR | os.O_NONBLOCK)
    runner_log.parent.mkdir(parents=True, exist_ok=True)
    proc = None
    observed = bytearray()
    state = "login"
    step = 0
    step_start = 0
    shell_at = 0.0
    started = time.monotonic()
    try:
        with runner_log.open("wb") as runner_out:
            proc = subprocess.Popen(
                ["sh", runner], stdout=runner_out, stderr=subprocess.STDOUT,
                start_new_session=True)
            cursor = 0
            while time.monotonic() - started < deadline:
                if log_name.is_file():
                    with log_name.open("rb") as stream:
                        stream.seek(cursor)
                        chunk = stream.read()
                        cursor += len(chunk)
                    observed.extend(chunk)
                text = observed.decode("utf-8", errors="replace").replace("\r", "")
                if "[FAIL]" in text or "TIOCSCTTY: Operation not permitted" in text:
                    raise RuntimeError("guest reported real boot/TTY failure")

                if state == "login" and "naseed login:" in text:
                    print("[M193 UART] original getty prompt; typing root", flush=True)
                    sent(hold, "root\r")
                    state = "password"
                elif state == "password" and "Password:" in text:
                    print("[M193 UART] original login asks for password", flush=True)
                    sent(hold, PASSWORD + "\r")
                    state = "shell"
                elif state == "shell" and (
                    "[EXEC] replaced process with lwext4 ELF: /bin/ash" in text or
                    shell_prompt(text)
                ):
                    # Original login has now launched its configured root ash.
                    if shell_at == 0:
                        shell_at = time.monotonic()
                    if time.monotonic() - shell_at >= 0.6:
                        print("[M193 UART] actual root shell; executing commands", flush=True)
                        step_start = len(text)
                        sent(hold, COMMANDS[step][0])
                        state = "commands"
                elif state == "commands":
                    command, expected, description = COMMANDS[step]
                    segment = text[step_start:]
                    okay = ("[EXEC] replaced process with lwext4 ELF: /bin/sh" in segment
                            if expected is None else output_line(segment, expected))
                    if okay:
                        print(f"[M193 UART] verified {description}", flush=True)
                        step += 1
                        if step == len(COMMANDS):
                            missing = [marker for marker in PROOF if marker not in text]
                            if missing:
                                raise RuntimeError("missing physical QEMU proof: " + ", ".join(missing))
                            if "[EXEC] replaced process with lwext4 ELF: /bin/login" not in text:
                                raise RuntimeError("original /bin/login was never executed")
                            if "[EXEC] replaced process with lwext4 ELF: /bin/ash" not in text:
                                raise RuntimeError("original /bin/ash was never executed")
                            print("[M193 ALPINE INTERACTIVE LOGIN PASS] original ext4 Alpine "
                                  "init -> OpenRC -> getty -> password-authenticated login "
                                  "-> ash -> /bin/sh -> real output; timeout was NOT a PASS", flush=True)
                            return
                        step_start = len(text)
                        sent(hold, COMMANDS[step][0])

                if proc.poll() is not None:
                    raise RuntimeError(f"native QEMU runner exited before login proof: {proc.returncode}")
                time.sleep(0.10)
            raise RuntimeError(f"M193 login proof timed out at state={state}, step={step}")
    except Exception:
        print("[M193 ALPINE INTERACTIVE LOGIN FAIL] physical serial tail:", file=sys.stderr)
        print(observed.decode("utf-8", errors="replace")[-11000:], file=sys.stderr)
        raise
    finally:
        if proc is not None and proc.poll() is None:
            os.killpg(proc.pid, signal.SIGTERM)
            try:
                proc.wait(timeout=8)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
        os.close(hold)
        fifo.unlink()
        # Preserve exactly the actual serial bytes, not synthesized success.
        if observed:
            cleaned_name = log_name.name.removesuffix(".serial.log") + ".clean.log"
            log_name.with_name(cleaned_name).write_bytes(bytes(observed).replace(b"\r", b""))


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit("usage: m193-alpine-uart.py RUNNER SERIAL_LOG RUNNER_LOG FIFO")
    seconds = int(os.environ.get("NAS_M193_DIAG_SECONDS", "110"))
    run(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]), seconds + 35)


if __name__ == "__main__":
    main()
