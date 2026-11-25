#!/usr/bin/env python3
"""M255 native-QEMU DIAGNOSTIC for signed Debian sysvinit as the REAL PID1.

This probes original PID1 -> inittab sysinit -> service -> original init
respawn -> service, without an artificial PID1 exit or synthetic disk PASS.
Real sysvinit poweroff and clean guest ext4 umount remain separate gates.
"""

import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time


REQUIRED_ORDER = (
    b"[ROOTFS] physical ext4 root registered at /",
    b"[ROOTFS] executable PID1 from mounted ext4: /sbin/init",
    b"[M255 ROOT] original Debian sysvinit PID1 from mounted ext4: /sbin/init",
    b"[M255 SYSINIT] original Debian init launched inittab sysinit action",
    b"M255-SERVICE-FIRST-PID=",
    b"M255-SERVICE-RESPAWN-PID=",
)
EXEC_MARKERS = (
    b"[EXEC] replaced process with lwext4 ELF: /etc/m255-guest-sysinit.sh",
    b"[EXEC] replaced process with lwext4 ELF: /etc/m255-guest-service.sh",
    b"[EXEC] replaced process with lwext4 ELF: /usr/bin/sleep",
)


def inspect(raw: bytes) -> dict:
    data = raw.replace(b"\r", b"")
    if b"[FAIL]" in data or b"[M250 LAUNCH FAIL]" in data:
        raise ValueError("native guest reports explicit failure")
    cursor = -1
    for marker in REQUIRED_ORDER:
        next_pos = data.find(marker, cursor + 1)
        if next_pos < 0:
            raise ValueError("missing or out-of-order original sysvinit marker: " +
                             marker.decode())
        cursor = next_pos
    for marker in EXEC_MARKERS:
        if marker not in data:
            raise ValueError("no actual signed guest exec: " + marker.decode())
    first = re.findall(rb"(?m)^M255-SERVICE-FIRST-PID=([0-9]+)[ \t]*$", data)
    second = re.findall(rb"(?m)^M255-SERVICE-RESPAWN-PID=([0-9]+)[ \t]*$", data)
    if len(first) != 1 or len(second) != 1 or first[0] == second[0]:
        raise ValueError("original sysvinit did not respawn a different real child PID")
    if data.count(b"[DYN] real ELF interpreter loaded, entry=") < 4:
        raise ValueError("original glibc loader was not independently executed by init/children")
    return {"first_pid": int(first[0]), "respawn_pid": int(second[0])}


def run_lifecycle(port: str, runner: str, serial: Path, runner_log: Path,
                  fifo: Path, deadline: int, *, strict_stale: bool,
                  stale_error: str, fatal, complete, exited, timeout_error: str,
                  failure_banner: str, tail_limit: int) -> dict:
    """Shared UART ownership; only profile callbacks decide acceptance.

    A diagnostic may finish from a live guest event. A natural-shutdown
    profile must leave complete() disabled and accept only exited(0).
    Host process cleanup is never a success condition.
    """
    fifo.parent.mkdir(parents=True, exist_ok=True)
    if fifo.exists() or (strict_stale and serial.exists()):
        raise RuntimeError(stale_error)
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
            # The shutdown profile's deadline historically begins after spawn.
            if strict_stale:
                start = time.monotonic()
            while time.monotonic() - start < deadline:
                if serial.is_file():
                    with serial.open("rb") as stream:
                        stream.seek(offset)
                        chunk = stream.read()
                    seen.extend(chunk)
                    offset += len(chunk)
                fatal(seen)
                result = complete(seen) if complete is not None else None
                if result is not None:
                    return result
                code = proc.poll()
                if code is not None:
                    return exited(code, seen)
                time.sleep(.10)
            raise TimeoutError(timeout_error)
    except Exception:
        print(failure_banner + " real UART tail:\n" +
              bytes(seen).decode(errors="replace")[-tail_limit:], file=sys.stderr)
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


def run(port: str, runner: str, serial: Path, runner_log: Path,
        fifo: Path, deadline: int) -> dict:
    if port not in ("amd64", "riscv64", "arm64"):
        raise ValueError("unreviewed native ISA")

    def fatal(seen):
        if b"[FAIL]" in seen:
            raise RuntimeError("native guest reported failure")

    def complete(seen):
        if (b"M255-SERVICE-RESPAWN-PID=" in seen and
                b"[EXEC] replaced process with lwext4 ELF: /usr/bin/sleep" in seen):
            result = inspect(bytes(seen))
            print(f"[M255 ORIGINAL DEBIAN SYSVINIT PID1/RESPAWN NATIVE QEMU "
                  f"DIAGNOSTIC VERIFIED] {port}: first PID {result['first_pid']}, "
                  f"respawn PID {result['respawn_pid']}; "
                  "poweroff/durable ext4 NOT YET PASS", flush=True)
            return result

    def exited(code, _seen):
        raise RuntimeError(f"native runner ended before sysvinit respawn: rc={code}")

    return run_lifecycle(port, runner, serial, runner_log, fifo, deadline,
                         strict_stale=False, stale_error="stale QEMU UART FIFO: " + str(fifo),
                         fatal=fatal, complete=complete, exited=exited,
                         timeout_error=f"sysvinit native respawn was not observed within {deadline}s",
                         failure_banner="[M255 SYSVINIT DIAGNOSTIC FAIL]", tail_limit=14000)


def main() -> None:
    if len(sys.argv) != 6:
        raise SystemExit("usage: m255-debian-uart.py PORT RUNNER SERIAL RUNNER_LOG FIFO")
    run(sys.argv[1], sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]),
        Path(sys.argv[5]), int(os.environ.get("NAS_M255_QEMU_TIMEOUT", "420")))


if __name__ == "__main__":
    main()
