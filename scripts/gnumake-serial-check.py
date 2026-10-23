#!/usr/bin/env python3
"""Unmodified original GNU Make executes two actual shell processes in NaSeed."""
import os
import select
import subprocess
import sys
import time

if len(sys.argv) not in (2,3) or (len(sys.argv)==3 and sys.argv[2]!="native-exec"):
    sys.exit("usage: gnumake-serial-check.py ISO [native-exec]")
native_exec=len(sys.argv)==3
p = subprocess.Popen(["sh", "scripts/run.sh", sys.argv[1]],
    stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT, bufsize=0)
log = bytearray()
deadline = time.monotonic()+70
while time.monotonic() < deadline:
    readable, _, _ = select.select([p.stdout], [], [], .08)
    if readable:
        chunk = os.read(p.stdout.fileno(), 65536)
        if not chunk:
            break
        log.extend(chunk)
    # The kernel correctly flags unsuccessful user programs; stop QEMU
    # immediately rather than waiting the whole 70s budget after a FAIL.
    if b"[FAIL]" in log:
        break
    if p.poll() is not None:
        break
if p.poll() is None:
    p.kill()
p.wait(timeout=5)
while True:
    chunk = os.read(p.stdout.fileno(), 65536)
    if not chunk:
        break
    log.extend(chunk)
p.stdout.close()
out=bytes(log)
work="build/external/gnumake/amd64"
os.makedirs(work,exist_ok=True)
with open(work+"/serial.log","wb") as fd:
    fd.write(out)
sys.stdout.buffer.write(out[-11000:])
sys.stdout.buffer.flush()
markers=([b"[NATIVE-EXEC] GNU Make builds, chmods and runs a TinyCC ELF",
          b"naseed-make-recipe-one",
          b"naseed-make-recipe-two",
          b"naseed-native-tcc-elf-ran",
          b"[PASS] cmdline userspace exit 0"]
         if native_exec else
         [b"[GNUMAKE] exec original GNU Make",
          b"naseed-make-recipe-one",
          b"naseed-make-recipe-two",
          b"naseed-compiled-object",
          b"[PASS] cmdline userspace exit 0"])
if (p.returncode!=1 or any(x not in out for x in markers) or
    out.count(b"naseed-make-recipe-one")!=1 or
    out.count(b"naseed-make-recipe-two")!=1 or b"[FAIL]" in out):
    sys.exit(f"[GNUMAKE NASEED FAIL] qemu={p.returncode},"
             f"found={[x in out for x in markers]}; see serial.log")
print("[NATIVE EXEC NASEED PASS] original Make/TinyCC linked and ran tmpfs ELF"
      if native_exec else
      "[GNUMAKE NASEED PASS] unmodified GNU Make ran real Linux shell recipes in NaSeed")
