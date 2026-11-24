#!/usr/bin/env python3
"""M251: real native-QEMU UART drives original Debian/glibc coreutils.

The interactive shell is M250's already accepted original Debian dash -i
on physical ext4. Commands are sent one at a time, only after a real prompt.
No host-side chroot, qemu-user, recorded transcript, or echoed input counts
as guest output. The caller must independently verify the durable disk.
"""

import importlib.util
import os
from pathlib import Path
import sys


PROMPT = b"M250> "  # M250's genuine CTTY launcher remains unchanged.
COMMANDS = (
    (b"/usr/bin/mkdir /var/tmp/m251-dir\r", None,
     "signed Debian mkdir creates an ext4 directory"),
    (b"/usr/bin/printf 'gamma\\nalpha\\nbeta\\n' > /var/tmp/m251-dir/input\r", None,
     "signed Debian printf creates input on ext4"),
    (b"/usr/bin/head -n 1 /var/tmp/m251-dir/input\r", b"gamma",
     "signed Debian head reads physical ext4"),
    (b"/usr/bin/sort /var/tmp/m251-dir/input | /usr/bin/head -n 1\r", b"alpha",
     "signed Debian sort and head execute through a real pipe"),
    (b"/usr/bin/ls -1 /var/tmp/m251-dir\r", b"input",
     "signed Debian ls enumerates ext4 directory entries"),
    (b"/usr/bin/env M251_TOKEN=M251-ENV-OK /usr/bin/env\r", b"M251_TOKEN=M251-ENV-OK",
     "signed Debian env propagates variables through execve"),
    (b"/usr/bin/sleep 0 && /usr/bin/printf '%s\\n' M251-SLEEP-OK\r",
     b"M251-SLEEP-OK", "signed Debian sleep completes successfully"),
    (b"/usr/bin/printf '%s\\n' M251-DURABLE > /var/tmp/m251-dir/result\r", None,
     "guest persists a separate host-readable ext4 result"),
    (b"/usr/bin/rm /var/tmp/m251-dir/input\r", None,
     "signed Debian rm unlinks the temporary input"),
    (b"test ! -e /var/tmp/m251-dir/input && /usr/bin/printf '%s\\n' M251-RM-OK\r",
     b"M251-RM-OK", "dash verifies actual unlink, not only rm exit status"),
    (b"exit\r", None, "original interactive Debian dash exits PID1 cleanly"),
)

REQUIRED = (
    b"[ROOTFS] physical ext4 root registered at /",
    b"[ROOTFS] executable PID1 from mounted ext4: /sbin/m250-tty-launch",
    b"[M250 CTTY] real /dev/tty acquired",
    b"[EXEC] replaced process with lwext4 ELF: /usr/bin/dash",
    b"[LWEXT4 GUEST] clean upstream unmount and durable flush",
    b"[PASS] cmdline userspace exit 0",
)
NEW_COREUTILS = (b"ls", b"head", b"sort", b"mkdir", b"rm", b"env", b"sleep")
NATIVE_EXIT = {
    "riscv64": b"[RV64] userspace debug-exit status=0",
    "arm64": b"[ARM64] userspace debug-exit status=0",
    "amd64": b"[PASS] cmdline userspace exit 0",
}


# Reuse the original M250 native UART transport.
spec = importlib.util.spec_from_file_location("m250_uart_transport_for_m251",
                                              Path(__file__).resolve().with_name("m250-debian-uart.py"))
assert spec and spec.loader
transport = importlib.util.module_from_spec(spec)
spec.loader.exec_module(transport)
output_line = transport.output_line
prompt_boundary = transport.prompt_boundary
send = transport.send
capture = transport.capture


def run(port: str, runner: str, serial: Path, runner_log: Path,
        fifo: Path, deadline_seconds: int) -> None:
    transport.run_profile(port, runner, serial, runner_log, fifo, deadline_seconds,
                          commands=COMMANDS, required=REQUIRED,
                          programs=(*NEW_COREUTILS, b"printf"),
                          minimum_interpreters=9, profile="M251",
                          native_exit=NATIVE_EXIT,
                          pass_message=f"[M251 INTERACTIVE ORIGINAL DEBIAN COREUTILS GUEST PASS] {port} "
                                       f"{len(NEW_COREUTILS)} signed new binaries, "
                                       f"{len(COMMANDS)} real UART commands, original glibc, "
                                       "native QEMU exit 0 and clean ext4 unmount")


def main() -> None:
    if len(sys.argv) != 6:
        raise SystemExit("usage: m251-debian-uart.py ISA RUNNER SERIAL_LOG RUNNER_LOG FIFO")
    port = sys.argv[1]
    budget = int(os.environ.get("NAS_M251_QEMU_TIMEOUT", "360"))
    run(port, sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]), Path(sys.argv[5]),
        budget + 60)


if __name__ == "__main__":
    main()
