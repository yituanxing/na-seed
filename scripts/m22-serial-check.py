#!/usr/bin/env python3
"""Only inject real QEMU serial bytes after a child reader lets parent run."""
import os
import select
import subprocess
import sys
import time
if len(sys.argv) not in (2,3):
    sys.exit("usage: m22-serial-check.py ISO [amd64|riscv64|arm64]")
port=sys.argv[2] if len(sys.argv)==3 else "amd64"
if port not in ("amd64","riscv64","arm64"):sys.exit("unknown ISA")
launcher=(["sh","scripts/run.sh",sys.argv[1]] if port=="amd64"
          else ["sh","scripts/m18-port-qemu.sh",port,sys.argv[1]])
p=subprocess.Popen(launcher,stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                   stderr=subprocess.STDOUT,bufsize=0)
log=bytearray()
sent=False
end=time.monotonic()+(80 if port!="amd64" else 50)
try:
    while time.monotonic()<end:
        r,_,_=select.select([p.stdout],[],[],0.05)
        if r:
            data=os.read(p.stdout.fileno(),65536)
            if not data:break
            log.extend(data)
        if not sent and b"[M22] parent-progress" in log:
            for c in b"poll\r":
                p.stdin.write(bytes([c]))
                p.stdin.flush()
                time.sleep(0.012)
            sent=True
        if b"[PASS] cmdline userspace exit 0" in log and sent:break
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
exited=(p.returncode==1 if port=="amd64" else
        (b"[RV64] userspace debug-exit status=0" in out if port=="riscv64"
         else b"[ARM64] userspace debug-exit status=0" in out))
if not(sent and exited and
       b"[M22] parent-progress" in out and
       b"[M22] child-tty-poll-ok" in out and
       b"[M22] poll-events-ok" in out and b"[M22] pipe-poll-ok" in out and
       b"[PASS] cmdline userspace exit 0" in out and
       b"[DEADLOCK]" not in out and b"[FAIL]" not in out):
    sys.exit("[M22 REAL POLL WAKE FAIL] %s input=%s qemu_rc=%s" %
             (port,sent,p.returncode))
print("[M22 POLL WAKE PASS] %s physical UART woke parked child" % port)
