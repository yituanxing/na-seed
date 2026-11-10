#!/usr/bin/env python3
"""M257: signed Debian SysVinit runlevel-0 natural QEMU poweroff oracle.

Unlike M255/M256's bounded diagnostic, observing an inittab marker never
ends this process. The native runner MUST exit on its own after genuine
reboot(POWER_OFF), closed Files, clean ext4 umount, and PSCI/SBI/debug-exit.
Offline physical ext4 fsck and nonce readback run as a separate postflight.
"""

import importlib.util
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("m255_m257", HERE / "m255-debian-uart.py")
assert spec and spec.loader
m255 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m255)

REQUIRED_IN_ORDER = (
    b"[M256 RUN TMPFS] real mount(2) attached /run",
    b"[M256 INITCTL] signaled real Debian PID1 to reopen control FIFO",
    b"[M255 SYSINIT] original Debian init launched inittab sysinit action",
    b"M255-SERVICE-FIRST-PID=",
    b"M255-SERVICE-RESPAWN-PID=",
    b"[M256 INITCTL] original PID1 FIFO is an actual named pipe",
    b"[EXEC] replaced process with lwext4 ELF: /usr/sbin/telinit",
    b"INIT: Switching to runlevel: 3",
    b"[M256 RUNLEVEL] original Debian init executed runlevel 3 inittab action",
    b"[M257 INITCTL] real /run/initctl named FIFO ready",
    b"[EXEC] replaced process with lwext4 ELF: /usr/sbin/telinit",
    b"INIT: Switching to runlevel: 0",
    b"[M257 RUNLEVEL0] original SysVinit launched reviewed runlevel 0 action",
    b"[M257 SYNC] original Debian sync completed for physical ext4 nonce",
    b"[EXEC] replaced process with lwext4 ELF: /usr/sbin/halt",
    b"[M257 DEBIAN HALT AUTHORIZED] original signed ext4 halt requested POWER_OFF",
    b"[M191 PID1 QUIESCED] no live non-init user Tasks",
    b"[LWEXT4 GUEST] clean upstream unmount and durable flush",
    b"[M257 DEBIAN POWER_OFF] original SysVinit halt; durable ext4 unmount",
    b"[PASS] cmdline userspace exit 0",
)
REQUIRED_ORIGINAL_ELF = (
    b"[EXEC] replaced process with lwext4 ELF: /usr/sbin/telinit",
    b"[EXEC] replaced process with lwext4 ELF: /usr/bin/sync",
    b"[EXEC] replaced process with lwext4 ELF: /usr/sbin/halt",
)


def inspect(raw: bytes, port: str) -> dict:
    if port not in ("amd64", "arm64", "riscv64"):
        raise ValueError("M257 unknown native ISA")
    result = m255.inspect(raw)
    data = raw.replace(b"\r", b"")
    if b"[FAIL]" in data or b"[M257 " in data and b" FAIL]" in data:
        raise ValueError("native Debian shutdown reported a failure")
    last = -1
    for marker in REQUIRED_IN_ORDER:
        position = data.find(marker, last + 1)
        if position < 0:
            raise ValueError("missing/out-of-order genuine Debian shutdown marker: "
                             + marker.decode())
        last = position
    for marker in REQUIRED_ORIGINAL_ELF:
        if marker not in data:
            raise ValueError("missing signed original Debian guest ELF: "
                             + marker.decode())
    platform = {
        "amd64": b"[PASS] cmdline userspace exit 0",
        "arm64": b"[ARM64] userspace debug-exit status=0",
        "riscv64": b"[RV64] userspace debug-exit status=0",
    }[port]
    if platform not in data:
        raise ValueError("missing platform shutdown transition: " + platform.decode())
    return result


def run(port: str, runner: str, serial: Path, runner_log: Path,
        fifo: Path, deadline: int) -> dict:
    if port not in ("amd64", "arm64", "riscv64"):
        raise ValueError("M257 unreviewed native ISA")
    fifo.parent.mkdir(parents=True, exist_ok=True)
    if fifo.exists() or serial.exists():
        raise RuntimeError("stale M257 UART stream; no replay as native evidence")
    os.mkfifo(fifo, 0o600)
    hold = os.open(fifo, os.O_RDWR | os.O_NONBLOCK)
    runner_log.parent.mkdir(parents=True, exist_ok=True)
    proc = None
    seen = bytearray()
    offset = 0
    try:
        with runner_log.open("wb") as log:
            proc = subprocess.Popen(["sh", runner], stdout=log,
                                    stderr=subprocess.STDOUT,
                                    start_new_session=True)
            end = time.monotonic() + deadline
            while time.monotonic() < end:
                if serial.is_file():
                    with serial.open("rb") as source:
                        source.seek(offset)
                        chunk = source.read()
                    seen.extend(chunk)
                    offset += len(chunk)
                if (b"[FAIL]" in seen or b"[M257 INITCTL FAIL]" in seen or
                        b"[M257 SHUTDOWN FAIL]" in seen or
                        b"[M257 HALT FAIL]" in seen or b"[M256 MOUNT FAIL]" in seen):
                    raise RuntimeError("real guest reported initctl/shutdown failure")
                code = proc.poll()
                if code is not None:
                    if code:
                        raise RuntimeError(f"QEMU native wrapper failed or timed out (rc={code})")
                    result = inspect(bytes(seen), port)
                    print(f"[M257 ORIGINAL DEBIAN SYSVINIT NATURAL QEMU POWER_OFF VERIFIED] "
                          f"{port}: signed telinit 0 -> original halt -> "
                          f"real PID1 quiesce -> clean ext4 umount -> autonomous exit; "
                          f"first PID {result['first_pid']}, "
                          f"respawn PID {result['respawn_pid']}; "
                          "physical disk postflight STILL REQUIRED", flush=True)
                    return result
                time.sleep(.10)
            raise TimeoutError(f"M257 natural guest shutdown not observed in {deadline}s")
    except Exception:
        print("[M257 ORIGINAL DEBIAN SHUTDOWN FAIL] real UART tail:\n" +
              bytes(seen).decode(errors="replace")[-13000:], file=sys.stderr)
        raise
    finally:
        # Cleanup happens only after a failure, never to turn a UART marker
        # into a fake natural poweroff. A success requires proc.poll()==0.
        if proc is not None and proc.poll() is None:
            try:
                os.killpg(proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                proc.wait(timeout=8)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
        os.close(hold)
        fifo.unlink()
        if seen:
            serial.with_name(serial.name.removesuffix(".serial.log") + ".clean.log").write_bytes(
                bytes(seen).replace(b"\r", b""))


def main() -> None:
    if len(sys.argv) != 6:
        raise SystemExit("usage: m257-debian-uart.py PORT RUNNER SERIAL LOG FIFO")
    run(sys.argv[1], sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]),
        Path(sys.argv[5]), int(os.environ.get("NAS_M257_QEMU_TIMEOUT", "700")))


if __name__ == "__main__":
    main()
