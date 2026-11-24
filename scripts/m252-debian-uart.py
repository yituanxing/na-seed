#!/usr/bin/env python3
"""M252: drive genuine Debian/glibc grep, sed and find over native QEMU UART.

The console is original Debian dash -i as the PID1 image on real writable
VirtIO ext4. Each command is sent only on the next real line-leading prompt;
terminal echo and prior commands are never accepted as guest stdout. The
caller separately authenticates ELF bytes and durable physical disk state.
"""

import importlib.util
import os
from pathlib import Path
import sys


PROMPT = b"M250> "
COMMANDS = (
    (b"/usr/bin/mkdir /var/tmp/m252-dir\r", None,
     "original Debian mkdir creates physical ext4 directory"),
    (b"/usr/bin/printf 'gamma\\nalpha\\nbeta\\n' > /var/tmp/m252-dir/input\r", None,
     "original Debian printf stages ext4 input"),
    (b"/usr/bin/grep '^alpha$' /var/tmp/m252-dir/input\r", b"alpha",
     "original signed grep reads ext4 and matches anchored line"),
    (b"/usr/bin/sed -n '1p' /var/tmp/m252-dir/input\r", b"gamma",
     "original signed sed selects a real ext4 line"),
    (b"/usr/bin/find /var/tmp/m252-dir -type f -name input\r",
     b"/var/tmp/m252-dir/input",
     "original signed find traverses a physical ext4 directory"),
    (b"/usr/bin/grep 'a' /var/tmp/m252-dir/input | /usr/bin/sed -n '2p'\r",
     b"alpha", "original signed grep-to-sed pipe"),
    (b"/usr/bin/sed 's/beta/BETA/' /var/tmp/m252-dir/input | /usr/bin/grep '^BETA$'\r",
     b"BETA", "original signed sed-to-grep transformation pipe"),
    (b"/usr/bin/find /var/tmp/m252-dir -type f | /usr/bin/grep '/input$'\r",
     b"/var/tmp/m252-dir/input", "original signed find-to-grep traversal pipe"),
    (b"/usr/bin/printf '%s\\n' M252-DURABLE > /var/tmp/m252-dir/result\r", None,
     "original Debian printf persists host-readable guest ext4 result"),
    (b"/usr/bin/find /var/tmp/m252-dir -type f -name result\r",
     b"/var/tmp/m252-dir/result", "original find discovers guest-created durable file"),
    (b"/usr/bin/grep -x M252-DURABLE /var/tmp/m252-dir/result\r",
     b"M252-DURABLE", "original grep rereads persisted ext4 content"),
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
NEW_PROGRAMS = (b"grep", b"sed", b"find")
ALSO_REQUIRED = (b"mkdir", b"printf")
NATIVE_EXIT = {
    "amd64": b"[PASS] cmdline userspace exit 0",
    "riscv64": b"[RV64] userspace debug-exit status=0",
    "arm64": b"[ARM64] userspace debug-exit status=0",
}


# Reuse the original M250 native UART transport.
spec = importlib.util.spec_from_file_location("m250_uart_transport_for_m252",
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
                          programs=(*NEW_PROGRAMS, *ALSO_REQUIRED),
                          minimum_interpreters=10, profile="M252",
                          native_exit=NATIVE_EXIT,
                          pass_message=f"[M252 INTERACTIVE ORIGINAL DEBIAN TEXT TOOLS GUEST PASS] {port} "
                                       f"{len(NEW_PROGRAMS)} new signed binaries, "
                                       f"{len(COMMANDS)} UART commands, original glibc, "
                                       "native QEMU exit 0 and clean ext4 unmount")


def main() -> None:
    if len(sys.argv) != 6:
        raise SystemExit("usage: m252-debian-uart.py ISA RUNNER SERIAL_LOG RUNNER_LOG FIFO")
    budget = int(os.environ.get("NAS_M252_QEMU_TIMEOUT", "360"))
    run(sys.argv[1], sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]),
        Path(sys.argv[5]), budget + 60)


if __name__ == "__main__":
    main()
