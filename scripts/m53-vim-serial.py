#!/usr/bin/env python3
"""Physical QEMU UART Vim editing; fail closed unless actual edited tmpfs survives."""
import os
import select
import subprocess
import sys
import time
from pathlib import Path

if len(sys.argv)!=3 or sys.argv[2] not in ("amd64","riscv64","arm64"):
    sys.exit("usage: m53-vim-serial.py IMAGE amd64|riscv64|arm64")
image,port=sys.argv[1:]
cmd=(["sh","scripts/run.sh",image] if port=="amd64" else
     ["sh","scripts/m18-port-qemu.sh",port,image])
proc=subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                      stderr=subprocess.STDOUT,bufsize=0)
log=bytearray()
sent=False
until=time.monotonic()+(95 if port!="amd64" else 75)
try:
    while time.monotonic()<until:
        ready,_,_=select.select([proc.stdout],[],[],0.05)
        if ready:
            chunk=os.read(proc.stdout.fileno(),65536)
            if not chunk:break
            log.extend(chunk)
        # Must observe real Vim's rendering of the actual file, not merely
        # the PID1 launcher declaring that an editor will start.
        if not sent and b"M53 original Vim serial session" in log:
            # Vim normal-mode start: gg0 -> insert literal bytes -> Escape ->
            # :write + quit. Each byte traverses physical UART and TTY.
            for byte in b"gg0iM53-LIVE-EDIT \x1b:wq\r":
                proc.stdin.write(bytes([byte]))
                proc.stdin.flush()
                time.sleep(0.024)
            sent=True
        if b"[PASS] cmdline userspace exit 0" in log and b"M53_ORIGINAL_VIM_REAL_TTY_EDIT_SAVE_PASS" in log:
            if port=="amd64" and proc.poll() is not None:break
            if port=="riscv64" and b"[RV64] userspace debug-exit status=0" in log:break
            if port=="arm64" and b"[ARM64] userspace debug-exit status=0" in log:break
        if proc.poll() is not None:break
    if proc.poll() is None:proc.kill()
    proc.wait(timeout=8)
    while True:
        extra=os.read(proc.stdout.fileno(),65536)
        if not extra:break
        log.extend(extra)
finally:
    proc.stdin.close()
    proc.stdout.close()
result=bytes(log).replace(b"\r",b"")
path=Path(f"build/external/m53-vim/{port}/live-serial.log")
path.parent.mkdir(parents=True,exist_ok=True)
path.write_bytes(result)
print(result[-6000:].decode("utf-8","replace"))
exit_ok=(proc.returncode==1 if port=="amd64" else
         (b"[RV64] userspace debug-exit status=0" in result if port=="riscv64"
          else b"[ARM64] userspace debug-exit status=0" in result))
if (not sent or not exit_ok or
        b"M53_ORIGINAL_VIM_REAL_TTY_EDIT_SAVE_PASS" not in result or
        b"[PASS] cmdline userspace exit 0" not in result or
        b"[FAIL]" in result or b"[DEADLOCK]" in result):
    sys.exit(f"[M53 FAIL] original Vim native UART {port}: sent={sent} rc={proc.returncode}, see {path}")
print(f"[M53 REAL VIM TTY PASS] {port} genuine UART file edit, write and exit")
