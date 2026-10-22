#!/usr/bin/env python3
"""Fail-closed native QEMU PIT IRQ / stock BusyBox CPU-hog acceptance.

The POSIX script intentionally never exits. We only stop QEMU AFTER seeing
the real user's output and kernel timer-IRQ context switch diagnostic.
A 22-second no-progress deadline proves a cooperative scheduler cannot
pass by sitting in its own syscalls. This is not a synthetic success.
"""
import os
import select
import signal
import subprocess
import sys
import time

if len(sys.argv)!=3:
    sys.exit("usage: m28-x86-serial-check.py ISO SERIAL_LOG")
iso, logfile=sys.argv[1:]
p=subprocess.Popen(["sh","scripts/run.sh",iso],stdin=subprocess.DEVNULL,
                   stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                   bufsize=0,start_new_session=True)
buf=bytearray()
deadline=time.monotonic()+22
marker=b"cpu-hog-did-not-starve-ash"
irq=b"[M28] timer-preempt from="
try:
    while time.monotonic()<deadline:
        readable,_,_=select.select([p.stdout],[],[],0.15)
        if readable:
            data=os.read(p.stdout.fileno(),65536)
            if data: buf.extend(data)
            elif p.poll() is not None: break
        if marker in buf and irq in buf:
            break
        if p.poll() is not None: break
finally:
    try:
        os.killpg(p.pid,signal.SIGKILL)
    except ProcessLookupError:
        pass
    p.wait(timeout=4)
    while True:
        readable,_,_=select.select([p.stdout],[],[],0)
        if not readable: break
        data=os.read(p.stdout.fileno(),65536)
        if not data: break
        buf.extend(data)
    p.stdout.close()
out=bytes(buf).replace(b"\r",b"")
with open(logfile,"wb") as file:
    file.write(out)
if marker not in out or irq not in out or b"[FAIL]" in out or b"[FAULT]" in out:
    sys.stderr.buffer.write(out[-9000:])
    sys.exit("[M28 X86 PREEMPT FAIL] missing unmodified BusyBox progress or real timer IRQ")
print("[M28 X86 PREEMPT PASS] QEMU IRQ32 preempted original BusyBox ash CPU spin")
