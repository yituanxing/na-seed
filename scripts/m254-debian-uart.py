#!/usr/bin/env python3
"""M254: run an original Debian/dash service-shaped script over live QEMU UART.

Retain every M252 acceptance step within *the same QEMU boot*. The additional
physical-ext4 script exercises a second dynamic dash, background process/
waitpid, a real pipe, environment, command substitution and durable ext4.
"""

import importlib.util
import os
from pathlib import Path
import sys


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("m252_real_uart_for_m254", HERE / "m252-debian-uart.py")
assert spec and spec.loader
m252 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m252)

ADDITIONAL = (
    (b"/etc/m254-guest-workload.sh\r", b"M254-SCRIPT-PASS",
     "original Debian dash runs child shell + async pipeline + wait + durable ext4"),
    (b"/usr/bin/printf 'M254-EXIT-%s\\n' \"$?\"\r", b"M254-EXIT-0",
     "interactive dash confirms actual child shell exit status 0"),
    (b"/usr/bin/grep -x M254-DURABLE /var/tmp/m254-lifecycle/result\r",
     b"M254-DURABLE", "original signed grep independently reads guest result"),
)
COMMANDS = (*m252.COMMANDS[:-1], *ADDITIONAL, m252.COMMANDS[-1])


def run(port: str, runner: str, serial: Path, runner_log: Path,
        fifo: Path, deadline_seconds: int) -> None:
    m252.COMMANDS = COMMANDS
    m252.run(port, runner, serial, runner_log, fifo, deadline_seconds)
    raw = serial.read_bytes().replace(b"\r", b"")
    # One original Debian dash is PID1; a *second* dynamic dash must start to
    # execute the script, rather than host-precomputed synthetic output.
    dash_marker = b"[EXEC] replaced process with lwext4 ELF: /usr/bin/dash"
    if raw.count(dash_marker) < 2:
        raise RuntimeError("original dynamic Debian dash child did not execute")
    if raw.count(b"[DYN] real ELF interpreter loaded, entry=") < 15:
        raise RuntimeError("missing dynamic interpreter evidence for shell workload")
    print(f"[M254 ORIGINAL DEBIAN SHELL LIFECYCLE GUEST PASS] {port}: "
          f"{len(COMMANDS)} live UART commands, signed glibc ELF, "
          "second dash, background wait, durable ext4, clean QEMU exit", flush=True)


def main() -> None:
    if len(sys.argv) != 6:
        raise SystemExit("usage: m254-debian-uart.py ISA RUNNER SERIAL RUNNER_LOG FIFO")
    budget = int(os.environ.get("NAS_M254_QEMU_TIMEOUT", "420"))
    run(sys.argv[1], sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]),
        Path(sys.argv[5]), budget + 60)


if __name__ == "__main__":
    main()
