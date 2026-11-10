#!/usr/bin/env python3
"""Offline M254 contract tests. These never claim a real guest PASS."""

import importlib.util
from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]


def imported(filename: str, name: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / filename)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


uart = imported("m254-debian-uart.py", "m254_original_debian_shell_uart_host")
disk = imported("m254-debian-ext4-disk.py", "m254_original_debian_shell_disk_host")


class DebianShellLifecycleHost(unittest.TestCase):
    def test_native_wrappers_do_not_fall_back_to_short_smoke_timeout(self):
        # Every architecture selects an explicit timeout for the new case.
        # The default x86 15-second cap previously killed genuine QEMU after
        # the eleventh inherited M252 UART command.
        for wrapper in ("probe-x86-busybox-parity.sh",
                        "probe-riscv64-busybox-matrix.sh",
                        "probe-aarch64-busybox-matrix.sh"):
            with self.subTest(wrapper=wrapper):
                content = (ROOT / "scripts" / wrapper).read_text()
                self.assertIn("m254-shell)", content)
                self.assertIn("NAS_M254_QEMU_TIMEOUT:-420", content)

    def test_real_three_isa_m252_baseline_is_preserved_in_one_boot(self):
        self.assertEqual(set(uart.m252.NATIVE_EXIT), {"amd64", "arm64", "riscv64"})
        self.assertEqual(uart.COMMANDS[:11], uart.m252.COMMANDS[:-1])
        self.assertEqual(len(uart.COMMANDS), 15)
        self.assertEqual(uart.COMMANDS[-1][0], b"exit\r")
        self.assertTrue(all(command.endswith(b"\r") for command, _, _ in uart.COMMANDS))

    def test_script_is_guest_executed_and_exit_zero_is_observed(self):
        self.assertEqual(uart.ADDITIONAL[0][0], b"/etc/m254-guest-workload.sh\r")
        self.assertEqual(uart.ADDITIONAL[0][1], b"M254-SCRIPT-PASS")
        self.assertIn(b'"$?"', uart.ADDITIONAL[1][0])
        self.assertEqual(uart.ADDITIONAL[1][1], b"M254-EXIT-0")
        self.assertEqual(uart.ADDITIONAL[2][1], b"M254-DURABLE")
        source = (ROOT / "scripts/m254-debian-uart.py").read_text()
        self.assertIn("raw.count(dash_marker) < 2", source)
        self.assertIn("m252.run(port, runner, serial", source)
        self.assertIn("missing dynamic interpreter evidence", source)

    def test_script_uses_original_signed_elf_concurrency_and_wait(self):
        script = ROOT / "scripts/m254-guest-workload.sh"
        self.assertEqual(subprocess.run(["dash", "-n", str(script)],
                                        capture_output=True).returncode, 0)
        source = script.read_text()
        self.assertTrue(source.startswith("#!/usr/bin/dash\n"))
        for needle in ("set -eu", "/usr/bin/sed", "/usr/bin/grep",
                       "worker=$!", 'wait "$worker"', "/usr/bin/env",
                       "/usr/bin/find", "M254-SCRIPT-PASS"):
            with self.subTest(needle=needle):
                self.assertIn(needle, source)
        self.assertLess(source.index('wait "$worker"'),
                        source.index("M254-DURABLE >"))

    def test_all_guest_outputs_require_post_shutdown_ext4_oracle(self):
        self.assertEqual(disk.RESULTS, {
            "var/tmp/m254-lifecycle/input": b"alpha\nbeta\ngamma\n",
            "var/tmp/m254-lifecycle/pipeline": b"M254-PIPE-OK\n",
            "var/tmp/m254-lifecycle/result": b"M254-DURABLE\n",
        })
        source = (ROOT / "scripts/m254-debian-ext4-disk.py").read_text()
        self.assertIn('expected if after else b""', source)
        self.assertIn("m191.check_fsck(part)", source)
        self.assertIn("m252.verify(port, root, disk", source)
        self.assertIn("m191.debugfs_cat(part", source)
        self.assertIn("unmodified reviewed guest script", source)

    def test_uart_stdout_guards_reject_echo_and_ps1_text(self):
        for fake in (b"/etc/m254-guest-workload.sh\n",
                     b"/usr/bin/printf M254-SCRIPT-PASS\n",
                     b"M254-SCRIPT-PASSX\n"):
            self.assertFalse(uart.m252.output_line(fake, b"M254-SCRIPT-PASS"))
        serial = b"M250> /usr/bin/env\nPS1=M250> \nM254-SCRIPT-PASS\nM250> "
        self.assertEqual(uart.m252.prompt_boundary(serial, len(uart.m252.PROMPT)),
                         serial.rfind(uart.m252.PROMPT))


if __name__ == "__main__":
    unittest.main()
