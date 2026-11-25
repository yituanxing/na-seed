#!/usr/bin/env python3
"""M256 physical QEMU original Debian sysvinit initctl diagnostic only.

Proves real PID1 open of a named tmpfs FIFO and signed Debian telinit
switching the actual inittab runlevel. Host SIGTERM cleanup does NOT count as
durable ext4 shutdown, so this script never prints full OS PASS.
"""

import importlib.util
import os
from pathlib import Path
import sys


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("m255_native_for_m256", HERE / "m255-debian-uart.py")
assert spec and spec.loader
m255 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m255)

RUN_MARKERS = (
    b"[M256 RUN TMPFS] real mount(2) attached /run",
    b"[M256 INITCTL] signaled real Debian PID1 to reopen control FIFO",
    b"[M256 INITCTL] original PID1 FIFO is an actual named pipe",
    b"INIT: Switching to runlevel: 3",
    b"[M256 RUNLEVEL] original Debian init executed runlevel 3 inittab action",
)
SIGNED_EXEC = b"[EXEC] replaced process with lwext4 ELF: /usr/sbin/telinit"


def inspect(raw: bytes) -> dict:
    first = m255.inspect(raw)
    data = raw.replace(b"\r", b"")
    for marker in RUN_MARKERS:
        if marker not in data:
            raise ValueError("missing original Debian initctl/runlevel evidence: " + marker.decode())
    require_order = (
        RUN_MARKERS[0], RUN_MARKERS[1],
        b"[M255 SYSINIT] original Debian init launched inittab sysinit action",
        b"M255-SERVICE-FIRST-PID=", b"M255-SERVICE-RESPAWN-PID=",
        RUN_MARKERS[2], SIGNED_EXEC, RUN_MARKERS[3], RUN_MARKERS[4],
    )
    last = -1
    for marker in require_order:
        at = data.find(marker, last + 1)
        if at < 0:
            raise ValueError("initctl lifecycle markers out of order: " + marker.decode())
        last = at
    if SIGNED_EXEC not in data:
        raise ValueError("Debian telinit must execute actual signed ELF")
    if b"[M256 MOUNT FAIL]" in data or b"[M256 INITCTL FAIL]" in data:
        raise ValueError("guest native initctl mount/control failure")
    return first


def run(port: str, runner: str, serial: Path, runner_log: Path,
        fifo: Path, deadline: int) -> dict:
    if port not in ("amd64", "riscv64", "arm64"):
        raise ValueError("unreviewed native ISA")

    def fatal(seen):
        if (b"[FAIL]" in seen or b"[M256 MOUNT FAIL]" in seen or
                b"[M256 INITCTL FAIL]" in seen):
            raise RuntimeError("real guest reported kernel/initctl failure")

    def complete(seen):
        if RUN_MARKERS[-1] in seen and SIGNED_EXEC in seen:
            result = inspect(bytes(seen))
            print(f"[M256 ORIGINAL DEBIAN SYSVINIT FIFO/TELINIT NATIVE QEMU "
                  f"DIAGNOSTIC VERIFIED] {port}: real initctl switched to "
                  f"runlevel 3; first PID {result['first_pid']}, "
                  f"respawn PID {result['respawn_pid']}; "
                  "poweroff/durable ext4 NOT YET PASS", flush=True)
            return result

    def exited(code, _seen):
        raise RuntimeError(f"guest exited before original initctl runlevel 3: rc={code}")

    return m255.run_lifecycle(port, runner, serial, runner_log, fifo, deadline,
                              strict_stale=False, stale_error="stale UART FIFO: " + str(fifo),
                              fatal=fatal, complete=complete, exited=exited,
                              timeout_error=f"signed initctl runlevel3 not observed in {deadline}s",
                              failure_banner="[M256 INITCTL DIAGNOSTIC FAIL]", tail_limit=16000)


def main() -> None:
    if len(sys.argv) != 6:
        raise SystemExit("usage: m256-debian-uart.py PORT RUNNER SERIAL RUNNER_LOG FIFO")
    run(sys.argv[1], sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]),
        Path(sys.argv[5]), int(os.environ.get("NAS_M256_QEMU_TIMEOUT", "480")))


if __name__ == "__main__":
    main()
