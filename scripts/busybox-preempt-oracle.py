#!/usr/bin/env python3
"""Real BusyBox ash pure-CPU fairness oracle on Linux.

The test writes ONE readiness line into a pipe, then executes a pure-CPU
loop without syscalls. The second pipeline process waits to read that line,
then prints a witness. On a cooperative kernel the hog never yields, so
the witness cannot appear, regardless of pipeline process launch order. A kernel
without timer-driven preemption cannot run the second runnable process.
Launch in a fresh process group and unconditionally kill descendants so a
passing host oracle never leaves an infinite loop behind.

A separate full-QEMU guest implementation can reuse *the same script* once
NaSeed has timer IRQs and correct interrupt-return context switching. This
host oracle must not be reported as NaSeed preemption passing.
"""
import os
import select
import signal
import subprocess
import sys
import time

binary = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "/usr/bin/busybox")
if not os.path.isfile(binary):
    sys.exit(f"[PREEMPT ORACLE FAIL] BusyBox binary not found: {binary}")
script = "(echo hog-started; while :; do :; done) | { read started; echo cpu-hog-did-not-starve-ash; }"
p = subprocess.Popen([binary, "sh", "-c", script], stdout=subprocess.PIPE,
                     stderr=subprocess.PIPE, start_new_session=True)
seen = b""
deadline = time.monotonic() + 5.0
try:
    while time.monotonic() < deadline and b"cpu-hog-did-not-starve-ash" not in seen:
        ready, _, _ = select.select([p.stdout], [], [], max(0.0, deadline - time.monotonic()))
        if not ready:
            break
        chunk = os.read(p.stdout.fileno(), 4096)
        if not chunk:
            break
        seen += chunk
    if b"cpu-hog-did-not-starve-ash" not in seen:
        sys.exit("[PREEMPT ORACLE FAIL] concurrent BusyBox ash starved")
    print("[PREEMPT LINUX ORACLE PASS] unmodified BusyBox ash CPU hog cannot starve shell")
finally:
    try:
        os.killpg(p.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    p.wait(timeout=3)
