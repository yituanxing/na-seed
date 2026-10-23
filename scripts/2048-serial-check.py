#!/usr/bin/env python3
"""Play *actual* original 2048 ELF through NaSeed's physical COM1 console."""
import os
import select
import subprocess
import sys
import time

if len(sys.argv) != 2:
    sys.exit("usage: 2048-serial-check.py ISO")
p = subprocess.Popen(["sh", "scripts/run.sh", sys.argv[1]],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    bufsize=0)
log = bytearray()
state = 0
moves = 0
next_event = time.monotonic()
deadline = time.monotonic()+65
try:
    while time.monotonic() < deadline:
        ready, _, _ = select.select([p.stdout], [], [], .04)
        if ready:
            data = os.read(p.stdout.fileno(), 65536)
            if not data:
                break
            log.extend(data)
        now = time.monotonic()
        if state == 0 and b"2048.c " in log:
            state = 1
        if state == 1 and now >= next_event:
            if moves < 4:
                p.stdin.write(b"asdw"[moves:moves+1])
                p.stdin.flush()
                moves += 1
                next_event = now + .26
            else:
                p.stdin.write(b"q")
                p.stdin.flush()
                state = 2
        if state == 2 and b"QUIT? (y/n)" in log:
            p.stdin.write(b"y")
            p.stdin.flush()
            state = 3
        if p.poll() is not None:
            break
    if p.poll() is None:
        p.kill()
    p.wait(timeout=5)
    while True:
        data = os.read(p.stdout.fileno(), 65536)
        if not data:
            break
        log.extend(data)
finally:
    p.stdout.close()
    p.stdin.close()
work="build/external/2048/amd64"
os.makedirs(work,exist_ok=True)
with open(work+"/serial.log","wb") as f:
    f.write(log)
out=bytes(log)
sys.stdout.buffer.write(out[-9000:])
sys.stdout.buffer.flush()
if (p.returncode != 1 or state != 3 or moves != 4 or
    out.count(b"2048.c ") < 2 or b"pts" not in out or
    b"QUIT? (y/n)" not in out or
    b"[PASS] cmdline userspace exit 0" not in out or b"[FAIL]" in out):
    sys.exit(f"[GAME NASEED FAIL] stage={state} moves={moves} "
        f"draws={out.count(b'2048.c ')} guest_exit={p.returncode}; see serial.log")
print("[GAME NASEED PASS] original 2048 ELF actually played on NaSeed COM1, graceful quit")
