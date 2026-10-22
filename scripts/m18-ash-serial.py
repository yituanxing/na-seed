#!/usr/bin/env python3
"""Real QEMU COM1 bytes --> unmodified upstream interactive BusyBox ash.

Each command is sent only after the previous live shell prompt/response;
the serial FIFO is not treated as an infinitely buffered artificial input.
"""
import os
import select
import subprocess
import sys
import time

if len(sys.argv) not in (2, 3):
    sys.exit("usage: m18-ash-serial.py ISO [amd64|riscv64|arm64]")
port = sys.argv[2] if len(sys.argv) == 3 else "amd64"
if port not in ("amd64", "riscv64", "arm64"):
    sys.exit("invalid native machine ISA")
commands = [
    (b"echo m18-alpha\r", b"m18-alpha"),
    (b"echo x | wc -c\r", b"2"),
    (b"pwd\r", b"/"),
    (b"echo m18-omega\r", b"m18-omega"),
    (b"exit\r", b"[PASS] cmdline userspace exit 0"),
]
launcher = (["sh", "scripts/run.sh", sys.argv[1]]
            if port == "amd64"
            else ["sh", "scripts/m18-port-qemu.sh", port, sys.argv[1]])
proc = subprocess.Popen(
    launcher,
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    bufsize=0,
)
log = bytearray()
cursor = 0
sent = 0
deadline = time.monotonic() + (80 if port != "amd64" else 50)
try:
    while time.monotonic() < deadline:
        readable, _, _ = select.select([proc.stdout], [], [], 0.04)
        if readable:
            chunk = os.read(proc.stdout.fileno(), 65536)
            if not chunk:
                break
            log.extend(chunk)
        normalized = bytes(log).replace(b"\r", b"")
        if sent < len(commands):
            expected = commands[sent - 1][1] if sent else None
            # Shell prompt is observed only after previous output evidence.
            boundary = normalized.find(b"M18> ", cursor)
            if boundary >= 0 and (expected is None or expected in normalized[cursor:boundary]):
                command = commands[sent][0]
                for byte in command:
                    proc.stdin.write(bytes([byte]))
                    proc.stdin.flush()
                    # Handshaking with actual COM1 RX byte FIFO, no bulk injection
                    time.sleep(0.012)
                sent += 1
                cursor = len(normalized)
        if b"[PASS] cmdline userspace exit 0" in normalized and sent == len(commands):
            break
        if proc.poll() is not None:
            break
    if proc.poll() is None:
        proc.kill()
    proc.wait(timeout=5)
    # QEMU exits at guest debug-exit before the host drains its stdout pipe.
    # Never mistake still-buffered final terminal/exit bytes for a kernel hang.
    while True:
        trailing = os.read(proc.stdout.fileno(), 65536)
        if not trailing:
            break
        log.extend(trailing)
finally:
    proc.stdout.close()
    proc.stdin.close()
result = bytes(log).replace(b"\r", b"")
sys.stdout.buffer.write(result)
sys.stdout.buffer.flush()
# x86 exits through ISA debug-exit(1); RV/ARM print the native exit
# marker, then WFI forever and are explicitly terminated by the host.
exit_ok = (proc.returncode == 1 if port == "amd64" else
           (b"[RV64] userspace debug-exit status=0" in result if port == "riscv64"
            else b"[ARM64] userspace debug-exit status=0" in result))
if sent != len(commands) or not exit_ok or \
   b"[PASS] cmdline userspace exit 0" not in result or \
   any(evidence not in result for _, evidence in commands) or \
   b"[FAIL]" in result or b"[DEADLOCK]" in result:
    sys.exit(f"[M18 REAL ASH FAIL] commands={sent}/{len(commands)} "
             f"QEMU_rc={proc.returncode}")
print("[M18 REAL ASH PASS] " + port +
      " native source BusyBox -i accepted 5 live serial commands")
