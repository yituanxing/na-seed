#!/usr/bin/env python3
"""Same genuine BusyBox ash pipe/CPU hog on Linux as in guest NaSeed.

The shell deliberately does not exit: success requires real user stdout
while the producer still spins. Always kill the fresh process group.
"""
import os
import select
import signal
import subprocess
import sys
import time
from pathlib import Path

binary = str(Path(sys.argv[1] if len(sys.argv)>1 else "build/source-busybox/amd64/busybox").resolve())
if not os.path.isfile(binary):
    sys.exit("[M28 ORACLE] missing unmodified BusyBox binary")
p = subprocess.Popen([binary, "sh", "tests/m28-preempt.sh"],
                     stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                     start_new_session=True)
seen = bytearray()
deadline=time.monotonic()+6.0
try:
    while time.monotonic()<deadline and b"cpu-hog-did-not-starve-ash" not in seen:
        fds,_,_=select.select([p.stdout],[],[],max(0,deadline-time.monotonic()))
        if not fds: break
        chunk=os.read(p.stdout.fileno(),4096)
        if not chunk: break
        seen.extend(chunk)
    if b"cpu-hog-did-not-starve-ash" not in seen:
        sys.exit("[M28 LINUX ORACLE FAIL] stock BusyBox CPU hog starved consumer")
    if p.poll() is not None:
        sys.exit("[M28 LINUX ORACLE FAIL] supposed infinite CPU hog exited")
    print("[M28 LINUX ORACLE PASS] original BusyBox ash consumer progressed with spinning producer")
finally:
    try:
        os.killpg(p.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    p.wait(timeout=4)
