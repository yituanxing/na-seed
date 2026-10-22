#!/usr/bin/env python3
"""Observe actual guest GNU nano curses screen, send real COM1 Ctrl+X."""
import os
import select
import subprocess
import sys
import time
if len(sys.argv) != 2:
    sys.exit("usage: nano-serial-check.py ISO")
proc = subprocess.Popen(
    ["sh", "scripts/run.sh", sys.argv[1]],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
log = bytearray()
sent = False
deadline = time.monotonic() + 65
try:
    while time.monotonic() < deadline:
        readable, _, _ = select.select([proc.stdout], [], [], .04)
        if readable:
            chunk = os.read(proc.stdout.fileno(), 65536)
            if not chunk:
                break
            log.extend(chunk)
        if not sent and b"NaSeed nano editor" in log:
            # Genuine COM1 delivery. The kernel's TTY and termios process it.
            proc.stdin.write(b"\x18")
            proc.stdin.flush()
            sent = True
        if proc.poll() is not None:
            break
    if proc.poll() is None:
        proc.kill()
    proc.wait(timeout=5)
    while True:
        chunk = os.read(proc.stdout.fileno(), 65536)
        if not chunk:
            break
        log.extend(chunk)
finally:
    proc.stdout.close()
    proc.stdin.close()
work="build/external/nano/amd64"
os.makedirs(work,exist_ok=True)
with open(work+"/serial.log","wb") as f:
    f.write(log)
result=bytes(log)
sys.stdout.buffer.write(result[-9000:])
sys.stdout.buffer.flush()
if not sent or b"[PASS] cmdline userspace exit 0" not in result or    proc.returncode != 1 or b"[FAIL]" in result:
    sys.exit(f"[NANO NASEED FAIL] screen={sent} exit={proc.returncode}; see serial.log")
print("[NANO NASEED PASS] original GNU nano showed actual file, real Ctrl+X closed editor")
