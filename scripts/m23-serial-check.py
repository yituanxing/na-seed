#!/usr/bin/env python3
"""Native QEMU serial Ctrl+C / Ctrl+Z, only AFTER parent group setup."""
import os
import select
import subprocess
import sys
import time
if len(sys.argv) not in (2,3):
    sys.exit("usage: m23-serial-check.py ISO [amd64|riscv64|arm64]")
port=sys.argv[2] if len(sys.argv)==3 else "amd64"
if port not in ("amd64","riscv64","arm64"):sys.exit("bad ISA")
command=(["sh","scripts/run.sh",sys.argv[1]] if port=="amd64" else
         ["sh","scripts/m18-port-qemu.sh",port,sys.argv[1]])
p=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                   stderr=subprocess.STDOUT,bufsize=0)
log=bytearray()
stage=0
deadline=time.monotonic()+(95 if port!="amd64" else 60)
try:
    while time.monotonic()<deadline:
        rd,_,_=select.select([p.stdout],[],[],0.05)
        if rd:
            data=os.read(p.stdout.fileno(),65536)
            if not data:break
            log.extend(data)
        if stage==0 and b"[M23] int-parent-ready" in log:
            p.stdin.write(b"\003");p.stdin.flush()
            stage=1
        if stage==1 and b"[M23] stop-parent-ready" in log:
            p.stdin.write(b"\032");p.stdin.flush()
            stage=2
        if stage==2 and b"[PASS] cmdline userspace exit 0" in log:
            if port=="amd64" and p.poll() is not None:break
            if port=="riscv64" and b"[RV64] userspace debug-exit status=0" in log:break
            if port=="arm64" and b"[ARM64] userspace debug-exit status=0" in log:break
        if p.poll() is not None:break
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
exitok=(p.returncode==1 if port=="amd64" else
       (b"[RV64] userspace debug-exit status=0" in out if port=="riscv64"
        else b"[ARM64] userspace debug-exit status=0" in out))
checks=[b"[M23] int-parent-ready",b"[M23] sigint-group-ok",
        b"[M23] stop-parent-ready",b"[M23] sigtstp-stop-ok",
        b"[M23] tty-signals-ok",b"[PASS] cmdline userspace exit 0"]
if stage!=2 or not exitok or not all(k in out for k in checks) or \
   b"[DEADLOCK]" in out or b"[M23 FAIL]" in out:
    sys.exit("[M23 REAL TTY SIGNAL FAIL] ISA=%s stage=%d exit=%s" %
             (port,stage,p.returncode))
print("[M23 TTY SIGNAL PASS] %s physical ^C foreground group + ^Z stop/CONT" % port)
