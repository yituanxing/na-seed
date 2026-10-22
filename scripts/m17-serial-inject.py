#!/usr/bin/env python3
"""Drive the actual QEMU stdio serial: send real bytes only after guest READY."""
import os
import select
import subprocess
import sys
import time

if len(sys.argv) != 2:
    sys.exit("usage: serial-inject.py ISO")
proc = subprocess.Popen(
    ["sh", "scripts/run.sh", sys.argv[1]],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    bufsize=0,
)
log = bytearray()
sent = False
limit = time.monotonic() + 25
try:
    while time.monotonic() < limit and proc.poll() is None:
        readable, _, _ = select.select([proc.stdout], [], [], 0.1)
        if readable:
            data = os.read(proc.stdout.fileno(), 65536)
            if not data:
                break
            log.extend(data)
        if not sent and b"[M17] tty-input-ready" in log:
            proc.stdin.write(b"hello\r")
            proc.stdin.flush()
            sent = True
    if proc.poll() is None:
        proc.kill()
    proc.wait(timeout=5)
finally:
    proc.stdout.close()
    proc.stdin.close()
result = bytes(log).replace(b"\r", b"")
sys.stdout.buffer.write(result)
sys.stdout.buffer.flush()
if not sent or b"m17-input-ok" not in result or b"[PASS] cmdline userspace exit 0" not in result or proc.returncode != 1:
    sys.exit(f"[M17 QEMU SERIAL FAIL] sent={sent} rc={proc.returncode}")
print("[M17 QEMU SERIAL PASS] real COM1 input after guest READY")
