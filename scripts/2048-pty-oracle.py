#!/usr/bin/env python3
"""Actually play the original game with real input on Linux PTY."""
import os
import pty
import select
import subprocess
import sys
import time

if len(sys.argv) != 2:
    sys.exit("usage: 2048-pty-oracle.py ELF")
master, slave = pty.openpty()
p = subprocess.Popen([sys.argv[1]], stdin=slave, stdout=slave, stderr=slave,
                     start_new_session=True, env={**os.environ,"TERM":"vt100"})
os.close(slave)
log = bytearray()
state = 0
moves = 0
next_event = time.monotonic()
deadline = time.monotonic() + 18
while time.monotonic() < deadline:
    ready, _, _ = select.select([master], [], [], .03)
    if ready:
        try:
            b = os.read(master, 65536)
        except OSError:
            break
        if not b:
            break
        log.extend(b)
    now = time.monotonic()
    if state == 0 and b"2048.c " in log:
        state = 1
    if state == 1 and now >= next_event:
        if moves < 4:
            os.write(master, b"asdw"[moves:moves+1])
            moves += 1
            next_event = now + .26
        else:
            os.write(master, b"q")
            state = 2
    if state == 2 and b"QUIT? (y/n)" in log:
        os.write(master, b"y")
        state = 3
    if p.poll() is not None:
        break
if p.poll() is None:
    p.kill()
p.wait(timeout=3)
os.close(master)
if (p.returncode != 0 or state != 3 or
    log.count(b"2048.c ") < 2 or b"pts" not in log or
    b"QUIT? (y/n)" not in log or
    b"Error! Cannot read keyboard input!" in log):
    sys.exit(f"[GAME LINUX PTY FAIL] exit={p.returncode} stage={state} "
             f"moves={moves} draws={log.count(b'2048.c ')} "
             f"tail={bytes(log)[-800:]!r}")
print(f"[GAME LINUX PTY PASS] original 2048 board, {moves} real moves, quit confirmation")
