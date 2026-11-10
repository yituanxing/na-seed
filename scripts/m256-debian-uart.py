#!/usr/bin/env python3
"""M256 physical QEMU original Debian sysvinit initctl diagnostic only.

Proves real PID1 open of a named tmpfs FIFO and signed Debian telinit
switching the actual inittab runlevel. Host SIGTERM cleanup does NOT count as
durable ext4 shutdown, so this script never prints full OS PASS.
"""

import importlib.util
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("m255_native_for_m256", HERE / "m255-debian-uart.py")
assert spec and spec.loader
m255 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m255)

RUN_MARKERS = (
    b"[M256 RUN TMPFS] real mount(2) attached /run",
    b"[M256 INITCTL] signaled real Debian PID1 to reopen control FIFO",
    b"[M256 INITCTL] original PID1 FIFO is an actual named pipe",
    b"[M256 INITCTL] signed Debian telinit submitted runlevel 3",
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
        RUN_MARKERS[2],
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
    fifo.parent.mkdir(parents=True, exist_ok=True)
    if fifo.exists():
        raise RuntimeError("stale UART FIFO: " + str(fifo))
    if serial.exists():
        serial.unlink()
    os.mkfifo(fifo, 0o600)
    hold = os.open(fifo, os.O_RDWR | os.O_NONBLOCK)
    runner_log.parent.mkdir(parents=True, exist_ok=True)
    proc = None
    seen = bytearray()
    offset = 0
    start = time.monotonic()
    try:
        with runner_log.open("wb") as log:
            proc = subprocess.Popen(["sh", runner], stdout=log,
                                    stderr=subprocess.STDOUT, start_new_session=True)
            while time.monotonic() - start < deadline:
                if serial.is_file():
                    with serial.open("rb") as source:
                        source.seek(offset)
                        chunk = source.read()
                        seen.extend(chunk)
                        offset += len(chunk)
                if b"[FAIL]" in seen or b"[M256 MOUNT FAIL]" in seen:
                    raise RuntimeError("real guest reported kernel/initctl failure")
                if RUN_MARKERS[-1] in seen and SIGNED_EXEC in seen:
                    result = inspect(bytes(seen))
                    print(f"[M256 ORIGINAL DEBIAN SYSVINIT FIFO/TELINIT NATIVE QEMU "
                          f"DIAGNOSTIC VERIFIED] {port}: real initctl switched to "
                          f"runlevel 3; first PID {result['first_pid']}, "
                          f"respawn PID {result['respawn_pid']}; "
                          "poweroff/durable ext4 NOT YET PASS", flush=True)
                    return result
                if proc.poll() is not None:
                    raise RuntimeError(f"guest exited before original initctl runlevel 3: "
                                       f"rc={proc.returncode}")
                time.sleep(.10)
            raise TimeoutError(f"signed initctl runlevel3 not observed in {deadline}s")
    except Exception:
        print("[M256 INITCTL DIAGNOSTIC FAIL] real UART tail:\n" +
              bytes(seen).decode(errors="replace")[-16000:], file=sys.stderr)
        raise
    finally:
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
        raise SystemExit("usage: m256-debian-uart.py PORT RUNNER SERIAL RUNNER_LOG FIFO")
    run(sys.argv[1], sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]),
        Path(sys.argv[5]), int(os.environ.get("NAS_M256_QEMU_TIMEOUT", "480")))


if __name__ == "__main__":
    main()
