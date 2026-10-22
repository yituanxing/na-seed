#!/usr/bin/env python3
"""Physical UART serial job-control test, same on all three QEMU kernels.

Do NOT inject a character before waitpid has reported genuine SIGTTIN
and the parent has explicitly made the stopped child foreground.
"""
import os
import select
import subprocess
import sys
import time

if len(sys.argv) not in (2,3):
    sys.exit("usage: m25-serial-check.py ISO [amd64|riscv64|arm64]")
port=sys.argv[2] if len(sys.argv)==3 else "amd64"
if port not in ("amd64","riscv64","arm64"):sys.exit("unknown architecture")
launcher=(["sh","scripts/run.sh",sys.argv[1]] if port=="amd64" else
          ["sh","scripts/m18-port-qemu.sh",port,sys.argv[1]])
p=subprocess.Popen(launcher,stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                   stderr=subprocess.STDOUT,bufsize=0)
log=bytearray()
sent=False
deadline=time.monotonic()+(95 if port!="amd64" else 65)
try:
    while time.monotonic()<deadline:
        readable,_,_=select.select([p.stdout],[],[],0.06)
        if readable:
            data=os.read(p.stdout.fileno(),65536)
            if not data:break
            log.extend(data)
        if not sent and b"[M25] input-ready" in log:
            for b in b"bgwake\r":
                p.stdin.write(bytes([b]))
                p.stdin.flush()
                time.sleep(0.012)
            sent=True
        if p.poll() is not None:break
        if b"[PASS] cmdline userspace exit 0" in log and sent:
            # On x86 QEMU exits via isa-debug-exit with status 1. The
            # serial marker can precede the process exit by one poll:
            # killing the still-running QEMU here gives rc=-9 despite all
            # real hardware tests passing. Wait for the ACTUAL exit, and
            # still fail at the deadline if it never happens.
            if port=="riscv64" and b"[RV64] userspace debug-exit status=0" in log:break
            if port=="arm64" and b"[ARM64] userspace debug-exit status=0" in log:break
    if p.poll() is None:p.kill()
    p.wait(timeout=5)
    while True:
        data=os.read(p.stdout.fileno(),65536)
        if not data:break
        log.extend(data)
finally:
    p.stdin.close()
    p.stdout.close()
out=bytes(log).replace(b"\r",b"")
sys.stdout.buffer.write(out)
sys.stdout.buffer.flush()
exit_ok=(p.returncode==1 if port=="amd64" else
         (b"[RV64] userspace debug-exit status=0" in out if port=="riscv64"
          else b"[ARM64] userspace debug-exit status=0" in out))
checks=[b"[M25] sigttin-stop-ok",b"[M25] input-ready",
        b"[M25] background-read-ok",b"[M25] sigttou-stop-ok",
        b"[M25] background-write-ok",b"[M25] ignored-read-eio-ok",
        b"[M25] tty-background-jobs-ok",b"[PASS] cmdline userspace exit 0"]
if not sent or not exit_ok or any(x not in out for x in checks) or \
   b"[DEADLOCK]" in out or b"[M25 FAIL]" in out:
    sys.exit(f"[M25 REAL TTY JOB FAIL] arch={port} sent={sent} rc={p.returncode}")
print("[M25 TTY JOB PASS] %s background SIGTTIN/SIGTTOU, SIGCONT and EIO" % port)
