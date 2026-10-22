#!/usr/bin/env python3
"""Run the same POSIX script under genuine, pinned BusyBox on host Linux.

Start a new process group; even a failing/hung oracle cannot leave the
infinite-loop child behind. This proves expected Linux behavior, NOT
NaSeed's kernel preemption. The guest gate separately verifies timer IRQs.
"""
import os
import signal
import subprocess
import sys
from pathlib import Path

binary = str(Path(sys.argv[1] if len(sys.argv)>1 else "build/source-busybox/amd64/busybox").resolve())
if not os.path.isfile(binary):
    sys.exit("[M28 ORACLE] missing unmodified BusyBox binary")
p = subprocess.Popen([binary, "sh", "tests/m28-preempt.sh"],
                     stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                     start_new_session=True)
try:
    out, err = p.communicate(timeout=6)
    if p.returncode != 0 or b"cpu-hog-did-not-starve-ash" not in out:
        sys.exit("[M28 ORACLE FAIL] rc=%r out=%r err=%r" %
                 (p.returncode, out[-1000:], err[-1000:]))
    print("[M28 LINUX ORACLE PASS] upstream BusyBox ash CPU hog was preempted")
except subprocess.TimeoutExpired:
    sys.exit("[M28 LINUX ORACLE FAIL] BusyBox CPU-hog script timed out")
finally:
    try:
        os.killpg(p.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    p.wait(timeout=4)
