#!/usr/bin/env python3
"""Original GNU nano in a host Linux PTY, same file and Ctrl+X semantics."""
import fcntl
import os
import pty
import select
import struct
import subprocess
import sys
import termios
import time
if len(sys.argv) != 2:
    sys.exit("usage: nano-pty-oracle.py NANO")
master, slave = pty.openpty()
# Real terminal dimensions; avoid zero-sized CI PTY artifacts.
fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
env = {**os.environ, "TERM": "vt100", "LC_ALL": "C", "HOME": "/tmp"}
process = subprocess.Popen(
    [sys.argv[1], "-I", "tests/nano-note.txt"],
    stdin=slave, stdout=slave, stderr=slave, env=env, start_new_session=True,
)
os.close(slave)
data = bytearray()
sent = False
end = time.monotonic() + 15
while time.monotonic() < end:
    ready, _, _ = select.select([master], [], [], .05)
    if ready:
        try:
            chunk = os.read(master, 65536)
        except OSError:
            break
        if not chunk:
            break
        data.extend(chunk)
    if not sent and b"NaSeed nano editor" in data:
        os.write(master, b"\x18")  # actual Ctrl+X, not a fake process exit
        sent = True
    if process.poll() is not None:
        break
if process.poll() is None:
    process.kill()
process.wait(timeout=3)
os.close(master)
if not sent or process.returncode != 0:
    sys.exit(f"[NANO LINUX PTY FAIL] rendered={sent} return={process.returncode} bytes={bytes(data)[-400:]!r}")
print("[NANO LINUX PTY PASS] original nano displayed real file and exited on Ctrl+X")
