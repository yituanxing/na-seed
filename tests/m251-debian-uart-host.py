#!/usr/bin/env python3
"""M251 negative UART contracts; never a substitute for a native guest run.

The real PASS is issued exclusively by the UART/QEMU runner after observing
each live prompt, independent guest stdout, the original ELF execution,
native exit zero and clean ext4 unmount. These tests exercise host guards.
"""

import importlib.util
from pathlib import Path
import unittest


BASE = Path(__file__).resolve().parents[1]


def imported(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


uart = imported(BASE / "scripts/m251-debian-uart.py", "m251_uart_tests")


class OriginalDebianInteractiveUARTContract(unittest.TestCase):
    def test_original_m250_controlling_tty_and_physical_ext4_reused(self):
        self.assertEqual(uart.PROMPT, b"M250> ")
        for fragment in (
            b"[ROOTFS] physical ext4 root registered at /",
            b"[ROOTFS] executable PID1 from mounted ext4: /sbin/m250-tty-launch",
            b"[M250 CTTY] real /dev/tty acquired",
            b"[EXEC] replaced process with lwext4 ELF: /usr/bin/dash",
            b"[LWEXT4 GUEST] clean upstream unmount and durable flush",
            b"[PASS] cmdline userspace exit 0",
        ):
            with self.subTest(marker=fragment):
                self.assertIn(fragment, uart.REQUIRED)

    def test_exactly_eleven_sequential_uart_commands_and_clean_exit(self):
        self.assertEqual(len(uart.COMMANDS), 11)
        self.assertEqual(uart.COMMANDS[-1][0], b"exit\r")
        self.assertTrue(all(command.endswith(b"\r") for command, _, _ in uart.COMMANDS))
        self.assertTrue(all(comment for _, _, comment in uart.COMMANDS))
        self.assertEqual(sum(expected is not None for _, expected, _ in uart.COMMANDS), 6)

    def test_seven_original_coreutils_and_original_printf_are_exercised(self):
        expected = {b"ls", b"head", b"sort", b"mkdir", b"rm", b"env", b"sleep"}
        self.assertEqual(set(uart.NEW_COREUTILS), expected)
        command_stream = b"\n".join(command for command, _, _ in uart.COMMANDS)
        for name in (*expected, b"printf"):
            with self.subTest(executable=name):
                self.assertIn(b"/usr/bin/" + name, command_stream)
        self.assertIn(b"/usr/bin/sort", command_stream)
        self.assertIn(b" | /usr/bin/head -n 1", command_stream)
        self.assertIn(b"/usr/bin/env M251_TOKEN=", command_stream)

    def test_real_read_and_sort_output_not_just_successful_exec(self):
        commands = [(command, expected) for command, expected, _ in uart.COMMANDS]
        self.assertIn((b"/usr/bin/head -n 1 /var/tmp/m251-dir/input\r", b"gamma"), commands)
        self.assertIn((b"/usr/bin/sort /var/tmp/m251-dir/input | /usr/bin/head -n 1\r",
                       b"alpha"), commands)
        self.assertIn((b"/usr/bin/ls -1 /var/tmp/m251-dir\r", b"input"), commands)

    def test_env_and_sleep_have_independent_output_markers(self):
        commands = [(command, expected) for command, expected, _ in uart.COMMANDS]
        self.assertIn((b"/usr/bin/env M251_TOKEN=M251-ENV-OK /usr/bin/env\r",
                       b"M251_TOKEN=M251-ENV-OK"), commands)
        self.assertIn((b"/usr/bin/sleep 0 && /usr/bin/printf '%s\\n' M251-SLEEP-OK\r",
                       b"M251-SLEEP-OK"), commands)

    def test_guest_durable_write_then_rm_then_independent_test(self):
        commands = [command for command, _, _ in uart.COMMANDS]
        write = b"/usr/bin/printf '%s\\n' M251-DURABLE > /var/tmp/m251-dir/result\r"
        unlink = b"/usr/bin/rm /var/tmp/m251-dir/input\r"
        missing = (b"test ! -e /var/tmp/m251-dir/input && "
                   b"/usr/bin/printf '%s\\n' M251-RM-OK\r")
        self.assertLess(commands.index(write), commands.index(unlink))
        self.assertLess(commands.index(unlink), commands.index(missing))
        self.assertEqual(uart.COMMANDS[commands.index(write)][1], None)
        self.assertEqual(uart.COMMANDS[commands.index(unlink)][1], None)
        self.assertEqual(uart.COMMANDS[commands.index(missing)][1], b"M251-RM-OK")

    def test_complete_guest_line_is_accepted_with_whitespace_and_cr_removed(self):
        self.assertTrue(uart.output_line(b"/usr/bin/head -n 1 ...\ngamma\n", b"gamma"))
        self.assertTrue(uart.output_line(b"/usr/bin/sort ...\n alpha \n", b"alpha"))
        self.assertTrue(uart.output_line(b"/usr/bin/env ...\nM251_TOKEN=M251-ENV-OK\n",
                                         b"M251_TOKEN=M251-ENV-OK"))

    def test_command_echo_is_not_independent_guest_output(self):
        for echoed, marker in (
            (b"/usr/bin/printf '%s\\n' M251-SLEEP-OK\n", b"M251-SLEEP-OK"),
            (b"/usr/bin/printf '%s\\n' M251-RM-OK\n", b"M251-RM-OK"),
            (b"/usr/bin/env M251_TOKEN=M251-ENV-OK /usr/bin/env\n",
             b"M251_TOKEN=M251-ENV-OK"),
            (b"/usr/bin/head -n 1 /var/tmp/m251-dir/input\n", b"gamma"),
            (b"M251-SLEEP-OKx\n", b"M251-SLEEP-OK"),
            (b"not-M251-RM-OK\n", b"M251-RM-OK"),
            (b"garbage M251-ENV-OK trailer\n", b"M251-ENV-OK"),
        ):
            with self.subTest(echoed=echoed):
                self.assertFalse(uart.output_line(echoed, marker))

    def test_three_native_ISAs_require_qemu_exit(self):
        self.assertEqual(set(uart.NATIVE_EXIT), {"riscv64", "arm64", "amd64"})
        self.assertIn(b"[RV64] userspace debug-exit status=0", uart.NATIVE_EXIT.values())
        self.assertIn(b"[ARM64] userspace debug-exit status=0", uart.NATIVE_EXIT.values())
        self.assertIn(b"[PASS] cmdline userspace exit 0", uart.NATIVE_EXIT.values())

    def test_runner_requires_original_elf_exec_glibc_and_native_rc_zero(self):
        source = (BASE / "scripts/m251-debian-uart.py").read_text()
        self.assertIn('proc.returncode != 0', source)
        self.assertIn('for name in (*NEW_COREUTILS, b"printf"):', source)
        self.assertIn('b"[EXEC] replaced process with lwext4 ELF: /usr/bin/" + name', source)
        self.assertIn('[DYN] real ELF interpreter loaded, entry=', source)
        self.assertIn('missing real glibc dynamic interpreter evidence', source)
        self.assertIn('os.mkfifo(', source)
        self.assertIn('send(hold, command)', source)
        self.assertIn('expected is not None and not output_line(segment, expected)', source)

    def test_negative_host_contract_does_not_fake_guest_pass(self):
        # M251 native guest PASS still needs actual QEMU serial, original ELF,
        # signed provenance and disk readback checked by the separate disk tool.
        source = (BASE / "scripts/m251-debian-uart.py").read_text()
        self.assertIn('native QEMU exit 0 and clean ext4 unmount', source)
        self.assertIn('native runner stopped early', source)


if __name__ == "__main__":
    unittest.main()
