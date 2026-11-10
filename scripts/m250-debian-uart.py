#!/usr/bin/env python3
"""End-to-end QEMU PL011/SBI UART -> original Debian glibc dash -i.

M193's real FIFO keyboard path is reused; nothing runs through host chroot,
qemu-user, guest stdin injection before TIOCSCTTY, or a fake shell. PASS means
each command was sent after an observed genuine dash prompt, returned actual
guest output, and the native-QEMU runner exited with guest exit status zero.
"""

import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time


PROMPT = b"M250> "
# None = intentionally silent redirection, still needs NEXT real prompt.
COMMANDS = (
    (b"/usr/bin/printf '%s\\n' M250-HELLO\r", b"M250-HELLO", "Debian printf on real UART"),
    (b"/usr/bin/cat /etc/m250-input\r", b"M250-EXT4-INPUT", "physical ext4 read"),
    (b"/usr/bin/cat /etc/m250-input | /usr/bin/wc -c\r", b"16", "native pipe and wc"),
    (b"/usr/bin/printf '%s\\n' M250-PERSISTED > /var/tmp/m250-persisted\r",
     None, "native dash redirection onto writable ext4"),
    (b"/usr/bin/cat /var/tmp/m250-persisted\r", b"M250-PERSISTED", "live guest write readback"),
    (b"/usr/bin/wc -c < /var/tmp/m250-persisted\r", b"15", "input redirection"),
    (b"/usr/bin/true && /usr/bin/printf '%s\\n' M250-COREUTILS-OK\r",
     b"M250-COREUTILS-OK", "native coreutils /usr/bin/true"),
    (b"exit\r", None, "original dash exits same PID1 cleanly"),
)

REQUIRED = (
    b"[ROOTFS] physical ext4 root registered at /",
    b"[ROOTFS] executable PID1 from mounted ext4: /sbin/m250-tty-launch",
    b"[M250 CTTY] real /dev/tty acquired",
    b"[EXEC] replaced process with lwext4 ELF: /usr/bin/dash",
    b"[LWEXT4 GUEST] clean upstream unmount and durable flush",
    b"[PASS] cmdline userspace exit 0",
)
NATIVE_EXIT = {"riscv64": b"[RV64] userspace debug-exit status=0",
               "arm64": b"[ARM64] userspace debug-exit status=0",
               "amd64": b"[PASS] cmdline userspace exit 0"}


def output_line(segment: bytes, value: bytes) -> bool:
    # A shell-command echo can contain the marker inside its arguments. Only
    # a complete, separate guest-output line counts (M193's distinction).
    return re.search(rb"(?:^|\n)[ \t]*" + re.escape(value) + rb"[ \t]*(?:\n|$)",
                     segment) is not None


def send(fd: int, command: bytes) -> None:
    for byte in command:
        os.write(fd, bytes((byte,)))
        time.sleep(0.018)  # hardware UART RX FIFO, not an infinite input pipe


def capture(log_name: Path, offset: int, received: bytearray) -> int:
    if log_name.is_file():
        with log_name.open("rb") as stream:
            stream.seek(offset)
            chunk = stream.read()
        received.extend(chunk)
        return offset + len(chunk)
    return offset


def run(port: str, runner: str, log_name: Path, runner_log: Path,
        fifo: Path, deadline_seconds: int) -> None:
    if port not in NATIVE_EXIT:
        raise ValueError("unrecognized native QEMU ISA")
    fifo.parent.mkdir(parents=True, exist_ok=True)
    if fifo.exists():
        raise RuntimeError("refusing stale serial FIFO: " + str(fifo))
    if log_name.exists():
        log_name.unlink()
    os.mkfifo(fifo, 0o600)
    hold = os.open(fifo, os.O_RDWR | os.O_NONBLOCK)
    runner_log.parent.mkdir(parents=True, exist_ok=True)
    proc = None
    raw = bytearray()
    logged = 0
    cursor = 0
    sent = 0
    first_prompt_seen = False
    started = time.monotonic()
    runner_finished_at = None
    try:
        with runner_log.open("wb") as outfile:
            proc = subprocess.Popen(["sh", runner], stdout=outfile,
                                    stderr=subprocess.STDOUT, start_new_session=True)
            while time.monotonic() - started < deadline_seconds:
                logged = capture(log_name, logged, raw)
                normalized = bytes(raw).replace(b"\r", b"")
                if b"[FAIL]" in normalized or b"[M250 LAUNCH FAIL]" in normalized:
                    raise RuntimeError("real guest reported a failure")
                if sent < len(COMMANDS):
                    boundary = normalized.find(PROMPT, cursor)
                    if boundary >= 0:
                        segment = normalized[cursor:boundary]
                        required = None if sent == 0 else COMMANDS[sent - 1][1]
                        if required is not None and not output_line(segment, required):
                            raise RuntimeError(
                                f"dash prompt without true guest-output line for command {sent}: "
                                + repr(required))
                        if not first_prompt_seen:
                            if b"[M250 CTTY] real /dev/tty acquired" not in normalized:
                                raise RuntimeError("unsigned helper skipped real TIOCSCTTY")
                            first_prompt_seen = True
                        command, _, description = COMMANDS[sent]
                        print(f"[M250 UART] {port} interactive step {sent+1}/{len(COMMANDS)}: "
                              f"{description}", flush=True)
                        cursor = boundary + len(PROMPT)
                        send(hold, command)
                        sent += 1

                if sent == len(COMMANDS) and NATIVE_EXIT[port] in normalized:
                    # Actual QEMU runner must also return 0; seeing a kernel
                    # marker without the host runner's verification is not PASS.
                    if proc.poll() is None:
                        time.sleep(0.15)
                        continue
                    logged = capture(log_name, logged, raw)
                    normalized = bytes(raw).replace(b"\r", b"")
                    if proc.returncode != 0:
                        raise RuntimeError("native runner failed after guest exit: " + str(proc.returncode))
                    missing = [marker for marker in REQUIRED if marker not in normalized]
                    if missing:
                        raise RuntimeError("missing physical-root/clean-unmount evidence: "
                                           + ", ".join(repr(item) for item in missing))
                    for name in (b"true", b"cat", b"wc", b"printf"):
                        marker = b"[EXEC] replaced process with lwext4 ELF: /usr/bin/" + name
                        if marker not in normalized:
                            raise RuntimeError("original Debian coreutils not executed: " + name.decode())
                    if normalized.count(b"[DYN] real ELF interpreter loaded, entry=") < 5:
                        raise RuntimeError("original signed glibc PT_INTERP not actually exercised")
                    print(f"[M250 INTERACTIVE DEBIAN UART GUEST PASS] {port} "
                          "8 sequential live commands; physical root; original dash/glibc; "
                          "pipe+redirection; clean native exit", flush=True)
                    return
                if proc.poll() is not None:
                    # The monitor can finish between two incremental UART
                    # reads. Give the log reader one final chance to drain
                    # the already-written kernel exit markers before FAIL.
                    if runner_finished_at is None:
                        runner_finished_at = time.monotonic()
                    if time.monotonic() - runner_finished_at >= 1.0:
                        raise RuntimeError(f"native runner stopped before full interactive proof: "
                                           f"rc={proc.returncode} sent={sent}")
                time.sleep(0.10)
            raise RuntimeError(f"real UART interactive timeout after "
                               f"{deadline_seconds}s, sent={sent}/{len(COMMANDS)}")
    except Exception:
        print("[M250 REAL DEBIAN UART FAIL] raw physical serial tail:\n" +
              bytes(raw).decode("utf-8", errors="replace")[-14000:], file=sys.stderr)
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
            clean = log_name.with_name(log_name.name.removesuffix(".serial.log") + ".clean.log")
            clean.write_bytes(bytes(raw).replace(b"\r", b""))


def main() -> None:
    if len(sys.argv) != 6:
        raise SystemExit("usage: m250-debian-uart.py ISA RUNNER SERIAL_LOG RUNNER_LOG FIFO")
    port = sys.argv[1]
    budget = int(os.environ.get("NAS_M250_QEMU_TIMEOUT", "300"))
    run(port, sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]), Path(sys.argv[5]),
        budget + 60)


if __name__ == "__main__":
    main()
