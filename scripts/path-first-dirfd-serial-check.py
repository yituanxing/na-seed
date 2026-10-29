#!/usr/bin/env python3
"""Fail-closed, real guest UART gate for the unchanged static dirfd C ELF."""
import os
import select
import subprocess
import sys
import time
from pathlib import Path

if len(sys.argv) not in (2, 3):
    sys.exit("usage: path-first-dirfd-serial-check.py image [amd64|riscv64|arm64]")
port = sys.argv[2] if len(sys.argv) == 3 else "amd64"
work = {
    "amd64": "build/busybox-source-parity-x86",
    "riscv64": "build/port-riscv64/full/matrix-source",
    "arm64": "build/port-aarch64/full/matrix-source",
}.get(port)
if work is None:
    sys.exit("[PATH FIRST DIRFD FAIL] invalid port")
command = (["sh", "scripts/run.sh", sys.argv[1]] if port == "amd64" else
           ["sh", "scripts/m18-port-qemu.sh", port, sys.argv[1]])
p = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.STDOUT, bufsize=0)
data = bytearray()
deadline = time.monotonic() + (125 if port != "amd64" else 75)
try:
    while time.monotonic() < deadline:
        readable, _, _ = select.select([p.stdout], [], [], .1)
        if readable:
            chunk = os.read(p.stdout.fileno(), 65536)
            if not chunk:
                break
            data.extend(chunk)
        if p.poll() is not None:
            break
    if p.poll() is None:
        p.kill()
    p.wait(timeout=5)
    while True:
        chunk = os.read(p.stdout.fileno(), 65536)
        if not chunk:
            break
        data.extend(chunk)
finally:
    p.stdin.close()
    p.stdout.close()
log = bytes(data).replace(b"\r", b"")
artifact = Path(work) / "path-first-dirfd.serial.log"
artifact.parent.mkdir(parents=True, exist_ok=True)
artifact.write_bytes(bytes(data))
sys.stdout.buffer.write(log)
sys.stdout.buffer.flush()
exit_ok = (p.returncode == 1 if port == "amd64" else
           b"[RV64] userspace debug-exit status=0" in log if port == "riscv64" else
           b"[ARM64] userspace debug-exit status=0" in log)
if (not exit_ok or b"PATH_FIRST_DIRFD_NATIVE_PASS" not in log or
    b"[PASS] cmdline userspace exit 0" not in log or
    b"[FAIL]" in log or b"[DEADLOCK]" in log or
    b"[PATH FIRST DIRFD FAIL]" in log):
    sys.exit("[PATH FIRST DIRFD NATIVE QEMU FAIL] %s exit=%s" %
             (port, p.returncode))
print("[PATH FIRST DIRFD NATIVE QEMU PASS] %s same Linux C ELF" % port)
