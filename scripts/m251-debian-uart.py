#!/usr/bin/env python3
"""M251: real native-QEMU UART drives original Debian/glibc coreutils.

The interactive shell is M250's already accepted original Debian dash -i
on physical ext4. Commands are sent one at a time, only after a real prompt.
No host-side chroot, qemu-user, recorded transcript, or echoed input counts
as guest output. The caller must independently verify the durable disk.
"""

import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time


PROMPT = b"M250> "  # M250's genuine CTTY launcher remains unchanged.
COMMANDS = (
    (b"/usr/bin/mkdir /var/tmp/m251-dir\r", None,
     "signed Debian mkdir creates an ext4 directory"),
    (b"/usr/bin/printf 'gamma\\nalpha\\nbeta\\n' > /var/tmp/m251-dir/input\r", None,
     "signed Debian printf creates input on ext4"),
    (b"/usr/bin/head -n 1 /var/tmp/m251-dir/input\r", b"gamma",
     "signed Debian head reads physical ext4"),
    (b"/usr/bin/sort /var/tmp/m251-dir/input | /usr/bin/head -n 1\r", b"alpha",
     "signed Debian sort and head execute through a real pipe"),
    (b"/usr/bin/ls -1 /var/tmp/m251-dir\r", b"input",
     "signed Debian ls enumerates ext4 directory entries"),
    (b"/usr/bin/env M251_TOKEN=M251-ENV-OK /usr/bin/env\r", b"M251_TOKEN=M251-ENV-OK",
     "signed Debian env propagates variables through execve"),
    (b"/usr/bin/sleep 0 && /usr/bin/printf '%s\\n' M251-SLEEP-OK\r",
     b"M251-SLEEP-OK", "signed Debian sleep completes successfully"),
    (b"/usr/bin/printf '%s\\n' M251-DURABLE > /var/tmp/m251-dir/result\r", None,
     "guest persists a separate host-readable ext4 result"),
    (b"/usr/bin/rm /var/tmp/m251-dir/input\r", None,
     "signed Debian rm unlinks the temporary input"),
    (b"test ! -e /var/tmp/m251-dir/input && /usr/bin/printf '%s\\n' M251-RM-OK\r",
     b"M251-RM-OK", "dash verifies actual unlink, not only rm exit status"),
    (b"exit\r", None, "original interactive Debian dash exits PID1 cleanly"),
)

REQUIRED = (
    b"[ROOTFS] physical ext4 root registered at /",
    b"[ROOTFS] executable PID1 from mounted ext4: /sbin/m250-tty-launch",
    b"[M250 CTTY] real /dev/tty acquired",
    b"[EXEC] replaced process with lwext4 ELF: /usr/bin/dash",
    b"[LWEXT4 GUEST] clean upstream unmount and durable flush",
    b"[PASS] cmdline userspace exit 0",
)
NEW_COREUTILS = (b"ls", b"head", b"sort", b"mkdir", b"rm", b"env", b"sleep")
NATIVE_EXIT = {
    "riscv64": b"[RV64] userspace debug-exit status=0",
    "arm64": b"[ARM64] userspace debug-exit status=0",
    "amd64": b"[PASS] cmdline userspace exit 0",
}


def output_line(segment: bytes, value: bytes) -> bool:
    """Only a complete guest stdout line, never a terminal command echo."""
    return re.search(rb"(?:^|\n)[ \t]*" + re.escape(value) + rb"[ \t]*(?:\n|$)",
                     segment) is not None


def send(fd: int, command: bytes) -> None:
    # Match M250's actual hardware RX FIFO pacing, not an unbounded pipe.
    for byte in command:
        os.write(fd, bytes((byte,)))
        time.sleep(0.018)


def capture(log: Path, offset: int, received: bytearray) -> int:
    if log.is_file():
        with log.open("rb") as stream:
            stream.seek(offset)
            chunk = stream.read()
        received.extend(chunk)
        return offset + len(chunk)
    return offset


def run(port: str, runner: str, serial: Path, runner_log: Path,
        fifo: Path, deadline_seconds: int) -> None:
    if port not in NATIVE_EXIT:
        raise ValueError("unreviewed native ISA")
    fifo.parent.mkdir(parents=True, exist_ok=True)
    if fifo.exists():
        raise RuntimeError("refusing stale real-UART FIFO: " + str(fifo))
    if serial.exists():
        serial.unlink()
    os.mkfifo(fifo, 0o600)
    hold = os.open(fifo, os.O_RDWR | os.O_NONBLOCK)
    runner_log.parent.mkdir(parents=True, exist_ok=True)
    proc = None
    raw = bytearray()
    logged = 0
    cursor = 0
    sent = 0
    first_prompt_seen = False
    runner_finished_at = None
    started = time.monotonic()
    try:
        with runner_log.open("wb") as outfile:
            proc = subprocess.Popen(["sh", runner], stdout=outfile,
                                    stderr=subprocess.STDOUT, start_new_session=True)
            while time.monotonic() - started < deadline_seconds:
                logged = capture(serial, logged, raw)
                normalized = bytes(raw).replace(b"\r", b"")
                if b"[FAIL]" in normalized or b"[M250 LAUNCH FAIL]" in normalized:
                    raise RuntimeError("native guest reported an explicit failure")
                if sent < len(COMMANDS):
                    boundary = normalized.find(PROMPT, cursor)
                    if boundary >= 0:
                        segment = normalized[cursor:boundary]
                        expected = None if sent == 0 else COMMANDS[sent - 1][1]
                        if expected is not None and not output_line(segment, expected):
                            raise RuntimeError(
                                f"prompt without genuine independent guest stdout line "
                                f"for command {sent}: {expected!r}")
                        if not first_prompt_seen:
                            if b"[M250 CTTY] real /dev/tty acquired" not in normalized:
                                raise RuntimeError("real controlling terminal was not acquired")
                            first_prompt_seen = True
                        command, _, description = COMMANDS[sent]
                        print(f"[M251 UART] {port} step {sent + 1}/{len(COMMANDS)}: "
                              f"{description}", flush=True)
                        cursor = boundary + len(PROMPT)
                        send(hold, command)
                        sent += 1

                if sent == len(COMMANDS) and NATIVE_EXIT[port] in normalized:
                    if proc.poll() is None:
                        time.sleep(0.15)
                        continue
                    logged = capture(serial, logged, raw)
                    normalized = bytes(raw).replace(b"\r", b"")
                    if proc.returncode != 0:
                        raise RuntimeError("native-QEMU runner failed: " + str(proc.returncode))
                    missing = [marker for marker in REQUIRED if marker not in normalized]
                    if missing:
                        raise RuntimeError("missing true mounted-root/exit evidence: "
                                           + ", ".join(repr(v) for v in missing))
                    for name in (*NEW_COREUTILS, b"printf"):
                        marker = b"[EXEC] replaced process with lwext4 ELF: /usr/bin/" + name
                        if marker not in normalized:
                            raise RuntimeError("original signed ELF not executed: "
                                               + name.decode())
                    if normalized.count(b"[DYN] real ELF interpreter loaded, entry=") < 9:
                        raise RuntimeError("missing real glibc dynamic interpreter evidence")
                    print(f"[M251 INTERACTIVE ORIGINAL DEBIAN COREUTILS GUEST PASS] {port} "
                          f"{len(NEW_COREUTILS)} signed new binaries, "
                          f"{len(COMMANDS)} real UART commands, original glibc, "
                          "native QEMU exit 0 and clean ext4 unmount", flush=True)
                    return

                if proc.poll() is not None:
                    # Avoid racing the last already-written QEMU serial chunk.
                    if runner_finished_at is None:
                        runner_finished_at = time.monotonic()
                    if time.monotonic() - runner_finished_at >= 1.0:
                        raise RuntimeError(f"native runner stopped early: "
                                           f"rc={proc.returncode}, commands={sent}")
                time.sleep(0.10)
            raise RuntimeError(f"real UART deadline {deadline_seconds}s "
                               f"with {sent}/{len(COMMANDS)} commands")
    except Exception:
        print("[M251 REAL DEBIAN UART FAIL] physical QEMU serial tail:\n"
              + bytes(raw).decode("utf-8", errors="replace")[-14000:], file=sys.stderr)
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
        if raw:
            clean = serial.with_name(serial.name.removesuffix(".serial.log") + ".clean.log")
            clean.write_bytes(bytes(raw).replace(b"\r", b""))


def main() -> None:
    if len(sys.argv) != 6:
        raise SystemExit("usage: m251-debian-uart.py ISA RUNNER SERIAL_LOG RUNNER_LOG FIFO")
    port = sys.argv[1]
    budget = int(os.environ.get("NAS_M251_QEMU_TIMEOUT", "360"))
    run(port, sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]), Path(sys.argv[5]),
        budget + 60)


if __name__ == "__main__":
    main()
