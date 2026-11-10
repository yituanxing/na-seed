#!/usr/bin/env python3
"""Inject physical Ctrl-C/Ctrl-Backslash into real three-ISA QEMU guests, gated
by explicit userspace parent-ready markers. No preboot input or fake signal.
"""
import os
import select
import subprocess
import sys
import time
from pathlib import Path

if len(sys.argv) not in (2, 3):
    sys.exit("usage: m105-signal-group-serial-check.py ISO [amd64|riscv64|arm64]")
port = sys.argv[2] if len(sys.argv) == 3 else "amd64"
if port not in ("amd64", "riscv64", "arm64"):
    sys.exit("[M105 FAIL] unknown target ISA")
command = (["sh", "scripts/run.sh", sys.argv[1]] if port == "amd64" else
           ["sh", "scripts/m18-port-qemu.sh", port, sys.argv[1]])
p = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.STDOUT, bufsize=0)
log = bytearray()
stage = 0
deadline = time.monotonic() + (125 if port != "amd64" else 75)
try:
    while time.monotonic() < deadline:
        readable, _, _ = select.select([p.stdout], [], [], 0.05)
        if readable:
            data = os.read(p.stdout.fileno(), 65536)
            if not data:
                break
            log.extend(data)
        if stage == 0 and b"[M105] default-parent-ready" in log:
            p.stdin.write(b"\003")
            p.stdin.flush()
            stage = 1
        if stage == 1 and b"[M105] caught-parent-ready" in log:
            p.stdin.write(b"\034")  # physical VQUIT: Ctrl-\\
            p.stdin.flush()
            stage = 2
        if stage == 2 and b"[M105] init-parent-ready" in log:
            p.stdin.write(b"\003")
            p.stdin.flush()
            stage = 3
        if stage == 3 and b"[PASS] cmdline userspace exit 0" in log:
            if port == "amd64" and p.poll() is not None:
                break
            if port == "riscv64" and b"[RV64] userspace debug-exit status=0" in log:
                break
            if port == "arm64" and b"[ARM64] userspace debug-exit status=0" in log:
                break
        if p.poll() is not None:
            break
    if p.poll() is None:
        p.kill()
    p.wait(timeout=5)
    while True:
        data = os.read(p.stdout.fileno(), 65536)
        if not data:
            break
        log.extend(data)
finally:
    p.stdin.close()
    p.stdout.close()
clean = bytes(log).replace(b"\r", b"")
artifact = Path({
    "amd64": "build/busybox-source-parity-x86/m105-signal-group.serial.log",
    "riscv64": "build/port-riscv64/full/matrix-source/m105-signal-group.serial.log",
    "arm64": "build/port-aarch64/full/matrix-source/m105-signal-group.serial.log",
}[port])
artifact.parent.mkdir(parents=True, exist_ok=True)
# Persist unedited guest UART bytes even for a failed marker/exit check;
# workflow failure artifacts must retain the offending kernel trace.
artifact.write_bytes(bytes(log))
sys.stdout.buffer.write(clean)
sys.stdout.buffer.flush()
exit_ok = (p.returncode == 1 if port == "amd64" else
           b"[RV64] userspace debug-exit status=0" in clean if port == "riscv64" else
           b"[ARM64] userspace debug-exit status=0" in clean)
checks = (b"[M105] default-parent-ready", b"M105_TTY_DEFAULT_SIGINT_PASS",
          b"[M105] caught-parent-ready", b"M105_TTY_HANDLER_ONCE_PASS",
          b"M105_GROUP_SHARED_PENDING_PASS",
          b"M105_ZOMBIE_LEADER_KILL_PASS", b"[M105] init-parent-ready",
          b"M105_TTY_PID1_WORKER_PASS", b"M105_SIGNAL_GROUP_NATIVE_PASS",
          b"[PASS] cmdline userspace exit 0")
if stage != 3 or not exit_ok or not all(marker in clean for marker in checks) or \
   b"[M105 FAIL]" in clean or b"[FAIL]" in clean or b"[DEADLOCK]" in clean:
    sys.exit("[M105 NATIVE QEMU FAIL] ISA=%s stage=%d exit=%s" %
             (port, stage, p.returncode))
print("[M105 NATIVE QEMU PASS] %s physical SIGINT/SIGQUIT, one-handler TGID,"
      " shared pending consumed by late-unmasked sibling, zombie leader"
      " and PID1 worker" % port)
