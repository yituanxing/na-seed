#!/usr/bin/env python3
"""Host-only negative guard tests; never a substitute for physical QEMU."""

import importlib.util
from pathlib import Path
import unittest


source = Path(__file__).with_name("m241-alpine-poweroff-uart.py")
spec = importlib.util.spec_from_file_location("naseed_m241_uart_test", source)
assert spec is not None and spec.loader is not None
uart = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uart)


class PhysicalEvidenceGuardTests(unittest.TestCase):
    def setUp(self):
        self.text = "\n".join((*uart.REQUIRED_ORDER,
                               "[EXEC] replaced process with lwext4 ELF: /bin/sh"))

    def test_complete_ordered_physical_markers(self):
        uart.verify_shutdown_uart(self.text)

    def test_cannot_promote_login_only(self):
        with self.assertRaisesRegex(RuntimeError, "ALPINE POWER_OFF"):
            uart.verify_shutdown_uart(self.text.replace(
                "[ALPINE POWER_OFF] original BusyBox; durable ext4 unmount", ""))

    def test_cannot_promote_out_of_order_flush(self):
        forged = self.text.replace(
            "[M191 PID1 QUIESCED] no live non-init user Tasks\n"
            "[LWEXT4 GUEST] clean upstream unmount and durable flush",
            "[LWEXT4 GUEST] clean upstream unmount and durable flush\n"
            "[M191 PID1 QUIESCED] no live non-init user Tasks")
        with self.assertRaisesRegex(RuntimeError, "out-of-order"):
            uart.verify_shutdown_uart(forged)

    def test_requires_original_poweroff_executable(self):
        with self.assertRaisesRegex(RuntimeError, "/sbin/poweroff"):
            uart.verify_shutdown_uart(self.text.replace(
                "[EXEC] replaced process with lwext4 ELF: /sbin/poweroff", ""))

    def test_cannot_promote_failure_log(self):
        with self.assertRaisesRegex(RuntimeError, "failure"):
            uart.verify_shutdown_uart(self.text + "\n[FAIL] exited with bad status")

    def test_requires_original_shell(self):
        with self.assertRaisesRegex(RuntimeError, "/bin/sh"):
            uart.verify_shutdown_uart(self.text.replace(
                "[EXEC] replaced process with lwext4 ELF: /bin/sh", ""))


if __name__ == "__main__":
    unittest.main()
