#!/usr/bin/env python3
"""M252: drive genuine Debian/glibc grep, sed and find over native QEMU UART.

The console is original Debian dash -i as the PID1 image on real writable
VirtIO ext4. Each command is sent only on the next real line-leading prompt;
terminal echo and prior commands are never accepted as guest stdout. The
caller separately authenticates ELF bytes and durable physical disk state.
"""

import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time


PROMPT = b"M250> "
COMMANDS = (
    (b"/usr/bin/mkdir /var/tmp/m252-dir\r", None,
     "original Debian mkdir creates physical ext4 directory"),
    (b"/usr/bin/printf 'gamma\\nalpha\\nbeta\\n' > /var/tmp/m252-dir/input\r", None,
     "original Debian printf stages ext4 input"),
    (b"/usr/bin/grep '^alpha$' /var/tmp/m252-dir/input\r", b"alpha",
     "original signed grep reads ext4 and matches anchored line"),
    (b"/usr/bin/sed -n '1p' /var/tmp/m252-dir/input\r", b"gamma",
     "original signed sed selects a real ext4 line"),
    (b"/usr/bin/find /var/tmp/m252-dir -type f -name input\r",
     b"/var/tmp/m252-dir/input",
     "original signed find traverses a physical ext4 directory"),
    (b"/usr/bin/grep 'a' /var/tmp/m252-dir/input | /usr/bin/sed -n '2p'\r",
     b"alpha", "original signed grep-to-sed pipe"),
    (b"/usr/bin/sed 's/beta/BETA/' /var/tmp/m252-dir/input | /usr/bin/grep '^BETA$'\r",
     b"BETA", "original signed sed-to-grep transformation pipe"),
    (b"/usr/bin/find /var/tmp/m252-dir -type f | /usr/bin/grep '/input$'\r",
     b"/var/tmp/m252-dir/input", "original signed find-to-grep traversal pipe"),
    (b"/usr/bin/printf '%s\\n' M252-DURABLE > /var/tmp/m252-dir/result\r", None,
     "original Debian printf persists host-readable guest ext4 result"),
    (b"/usr/bin/find /var/tmp/m252-dir -type f -name result\r",
     b"/var/tmp/m252-dir/result", "original find discovers guest-created durable file"),
    (b"/usr/bin/grep -x M252-DURABLE /var/tmp/m252-dir/result\r",
     b"M252-DURABLE", "original grep rereads persisted ext4 content"),
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
NEW_PROGRAMS = (b"grep", b"sed", b"find")
ALSO_REQUIRED = (b"mkdir", b"printf")
NATIVE_EXIT = {
    "amd64": b"[PASS] cmdline userspace exit 0",
    "riscv64": b"[RV64] userspace debug-exit status=0",
    "arm64": b"[ARM64] userspace debug-exit status=0",
}


def output_line(segment: bytes, value: bytes) -> bool:
    """A complete isolated stdout line, not an embedded command echo."""
    return re.search(rb"(?:^|\n)[ \t]*" + re.escape(value) + rb"[ \t]*(?:\n|$)",
                     segment) is not None


def prompt_boundary(serial: bytes, cursor: int) -> int:
    """Only the true line-leading shell prompt; ignore PS1=M250> in output."""
    match = re.search(rb"(?m)^" + re.escape(PROMPT), serial[cursor:])
    return -1 if match is None else cursor + match.start()


def send(fd: int, command: bytes) -> None:
    # Match M250/M251's known-good hardware UART FIFO RX pacing.
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
                    raise RuntimeError("native guest reported explicit failure")
                if sent < len(COMMANDS):
                    boundary = prompt_boundary(normalized, cursor)
                    if boundary >= 0:
                        segment = normalized[cursor:boundary]
                        expected = None if sent == 0 else COMMANDS[sent - 1][1]
                        if expected is not None and not output_line(segment, expected):
                            raise RuntimeError(
                                f"prompt without independent guest stdout "
                                f"for command {sent}: {expected!r}")
                        if not first_prompt_seen:
                            if b"[M250 CTTY] real /dev/tty acquired" not in normalized:
                                raise RuntimeError("no real controlling terminal")
                            first_prompt_seen = True
                        command, _, explanation = COMMANDS[sent]
                        print(f"[M252 UART] {port} step {sent + 1}/{len(COMMANDS)}: "
                              f"{explanation}", flush=True)
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
                        raise RuntimeError("native QEMU runner exit " + str(proc.returncode))
                    missing = [marker for marker in REQUIRED if marker not in normalized]
                    if missing:
                        raise RuntimeError("missing real ext4/TTY/exit evidence: "
                                           + ", ".join(repr(item) for item in missing))
                    for name in (*NEW_PROGRAMS, *ALSO_REQUIRED):
                        marker = b"[EXEC] replaced process with lwext4 ELF: /usr/bin/" + name
                        if marker not in normalized:
                            raise RuntimeError("original signed Debian ELF not executed: "
                                               + name.decode())
                    if normalized.count(b"[DYN] real ELF interpreter loaded, entry=") < 10:
                        raise RuntimeError("missing genuine glibc dynamic interpreter load")
                    print(f"[M252 INTERACTIVE ORIGINAL DEBIAN TEXT TOOLS GUEST PASS] {port} "
                          f"{len(NEW_PROGRAMS)} new signed binaries, "
                          f"{len(COMMANDS)} UART commands, original glibc, "
                          "native QEMU exit 0 and clean ext4 unmount", flush=True)
                    return

                if proc.poll() is not None:
                    if runner_finished_at is None:
                        runner_finished_at = time.monotonic()
                    if time.monotonic() - runner_finished_at >= 1.0:
                        raise RuntimeError(f"native runner stopped early: "
                                           f"rc={proc.returncode}, commands={sent}")
                time.sleep(0.10)
            raise RuntimeError(f"real UART deadline {deadline_seconds}s "
                               f"with {sent}/{len(COMMANDS)} commands")
    except Exception:
        print("[M252 REAL DEBIAN UART FAIL] physical QEMU serial tail:\n"
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
        raise SystemExit("usage: m252-debian-uart.py ISA RUNNER SERIAL_LOG RUNNER_LOG FIFO")
    budget = int(os.environ.get("NAS_M252_QEMU_TIMEOUT", "360"))
    run(sys.argv[1], sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]),
        Path(sys.argv[5]), budget + 60)


if __name__ == "__main__":
    main()
