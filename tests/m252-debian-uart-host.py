#!/usr/bin/env python3
"""Offline negative contracts for M252 original Debian grep/sed/find UART.

These must never be mistaken for genuine NaSeed native three-ISA acceptance.
"""

import importlib.util
from pathlib import Path
import unittest


BASE = Path(__file__).resolve().parents[1]
MODULE = BASE / "scripts/m252-debian-uart.py"


def imported(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


uart = imported(MODULE, "m252_original_debian_uart_tests")


class OriginalDebianTextToolsContract(unittest.TestCase):
    def test_preserves_native_original_dash_ctty_ext4(self):
        self.assertEqual(uart.PROMPT, b"M250> ")
        for evidence in (b"[ROOTFS] physical ext4 root registered at /",
                         b"[ROOTFS] executable PID1 from mounted ext4: /sbin/m250-tty-launch",
                         b"[M250 CTTY] real /dev/tty acquired",
                         b"[EXEC] replaced process with lwext4 ELF: /usr/bin/dash",
                         b"[LWEXT4 GUEST] clean upstream unmount and durable flush",
                         b"[PASS] cmdline userspace exit 0"):
            with self.subTest(evidence=evidence):
                self.assertIn(evidence, uart.REQUIRED)

    def test_twelve_sequential_real_uart_commands_and_clean_exit(self):
        self.assertEqual(len(uart.COMMANDS), 12)
        self.assertEqual(uart.COMMANDS[-1][0], b"exit\r")
        self.assertTrue(all(command.endswith(b"\r") for command, _, _ in uart.COMMANDS))
        self.assertEqual(sum(expect is not None for _, expect, _ in uart.COMMANDS), 8)

    def test_signed_glibc_programs_and_explicit_original_helper_elf(self):
        self.assertEqual(set(uart.NEW_PROGRAMS), {b"grep", b"sed", b"find"})
        self.assertEqual(set(uart.ALSO_REQUIRED), {b"mkdir", b"printf"})
        all_commands = b"\n".join(command for command, _, _ in uart.COMMANDS)
        for program in (*uart.NEW_PROGRAMS, *uart.ALSO_REQUIRED):
            with self.subTest(program=program):
                self.assertIn(b"/usr/bin/" + program, all_commands)

    def test_grep_sed_find_each_has_real_file_output(self):
        seq = [(cmd, expected) for cmd, expected, _ in uart.COMMANDS]
        self.assertIn((b"/usr/bin/grep '^alpha$' /var/tmp/m252-dir/input\r", b"alpha"), seq)
        self.assertIn((b"/usr/bin/sed -n '1p' /var/tmp/m252-dir/input\r", b"gamma"), seq)
        self.assertIn((b"/usr/bin/find /var/tmp/m252-dir -type f -name input\r",
                       b"/var/tmp/m252-dir/input"), seq)

    def test_three_composed_real_unix_pipes(self):
        commands = [(command, expected) for command, expected, _ in uart.COMMANDS]
        self.assertIn((b"/usr/bin/grep 'a' /var/tmp/m252-dir/input | "
                       b"/usr/bin/sed -n '2p'\r", b"alpha"), commands)
        self.assertIn((b"/usr/bin/sed 's/beta/BETA/' /var/tmp/m252-dir/input | "
                       b"/usr/bin/grep '^BETA$'\r", b"BETA"), commands)
        self.assertIn((b"/usr/bin/find /var/tmp/m252-dir -type f | "
                       b"/usr/bin/grep '/input$'\r", b"/var/tmp/m252-dir/input"), commands)

    def test_real_guest_durable_ext4_writes_and_readback(self):
        commands = [cmd for cmd, _, _ in uart.COMMANDS]
        write = b"/usr/bin/printf '%s\\n' M252-DURABLE > /var/tmp/m252-dir/result\r"
        find = b"/usr/bin/find /var/tmp/m252-dir -type f -name result\r"
        grep = b"/usr/bin/grep -x M252-DURABLE /var/tmp/m252-dir/result\r"
        self.assertIn(write, commands)
        self.assertLess(commands.index(write), commands.index(find))
        self.assertLess(commands.index(find), commands.index(grep))
        self.assertEqual(uart.COMMANDS[commands.index(find)][1],
                         b"/var/tmp/m252-dir/result")
        self.assertEqual(uart.COMMANDS[commands.index(grep)][1], b"M252-DURABLE")

    def test_isolated_stdout_accepted(self):
        for data, marker in ((b"/usr/bin/grep ...\nalpha\n", b"alpha"),
                             (b"/usr/bin/sed ...\n  BETA  \n", b"BETA"),
                             (b"/usr/bin/find ...\n/var/tmp/m252-dir/result\n",
                              b"/var/tmp/m252-dir/result")):
            with self.subTest(marker=marker):
                self.assertTrue(uart.output_line(data, marker))

    def test_command_echo_cannot_forge_guest_stdout(self):
        for raw, marker in (
                (b"/usr/bin/grep -x M252-DURABLE /var/tmp/m252-dir/result\n",
                 b"M252-DURABLE"),
                (b"/usr/bin/printf '%s\\n' M252-DURABLE > /var/tmp/m252-dir/result\n",
                 b"M252-DURABLE"),
                (b"/usr/bin/find /var/tmp/m252-dir -type f -name input\n",
                 b"/var/tmp/m252-dir/input"),
                (b"M252-DURABLEx\n", b"M252-DURABLE"),
                (b"/var/tmp/m252-dir/result.bak\n", b"/var/tmp/m252-dir/result")):
            with self.subTest(raw=raw):
                self.assertFalse(uart.output_line(raw, marker))

    def test_shell_prompt_ignores_ps1_stdout_and_echo(self):
        serial = (b"M250> /usr/bin/env\nHOME=/\nPS1=M250> \n"
                  b"M252-DURABLE\nM250> [SCHED] blocked pid=1\n")
        cursor = len(uart.PROMPT)
        boundary = uart.prompt_boundary(serial, cursor)
        self.assertEqual(boundary, serial.rfind(uart.PROMPT))
        self.assertEqual(uart.prompt_boundary(serial[:boundary], cursor), -1)
        self.assertTrue(uart.output_line(serial[cursor:boundary], b"M252-DURABLE"))

    def test_three_actual_full_system_qemu_architectures(self):
        self.assertEqual(set(uart.NATIVE_EXIT), {"amd64", "riscv64", "arm64"})
        self.assertIn(b"[RV64] userspace debug-exit status=0", uart.NATIVE_EXIT.values())
        self.assertIn(b"[ARM64] userspace debug-exit status=0", uart.NATIVE_EXIT.values())

    def test_runner_exits_zero_real_original_elf_and_dynamic_loader(self):
        source = MODULE.read_text(encoding="utf-8")
        for fragment in ("proc.returncode != 0",
                         "for name in (*NEW_PROGRAMS, *ALSO_REQUIRED):",
                         "[EXEC] replaced process with lwext4 ELF: /usr/bin/",
                         "[DYN] real ELF interpreter loaded, entry=",
                         "missing genuine glibc dynamic interpreter load",
                         "os.mkfifo(", "send(hold, command)", "native runner stopped early"):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, source)


if __name__ == "__main__":
    unittest.main()
