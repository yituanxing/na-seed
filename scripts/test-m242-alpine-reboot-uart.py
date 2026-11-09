#!/usr/bin/env python3
"""Host-only strict parsing guards; never substitute for a native reset."""

import importlib.util
import inspect
from pathlib import Path
import unittest
from unittest import mock


source = Path(__file__).with_name("m242-alpine-reboot-uart.py")
spec = importlib.util.spec_from_file_location("naseed_m242_uart_test", source)
assert spec is not None and spec.loader is not None
uart = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uart)


def physical_fixture():
    first = (*uart.BOOT,
             "[EXEC] replaced process with lwext4 ELF: /bin/sh",
             "M242_WRITE_RC=0",
             "M242_SYNC_RC=0",
             "[EXEC] replaced process with lwext4 ELF: /sbin/reboot",
             *uart.FLUSH,
             uart.RESET_MARKER)
    second = (*uart.BOOT,
              "[EXEC] replaced process with lwext4 ELF: /bin/sh",
              "M242_SECOND_BOOT_NONCE=" + uart.NONCE,
              "[EXEC] replaced process with lwext4 ELF: /sbin/poweroff",
              *uart.FLUSH,
              "[ALPINE POWER_OFF] original BusyBox; durable ext4 unmount",
              "[PASS] cmdline userspace exit 0")
    return "\n".join((*first, *second))


class TwoBootUARTGuardTests(unittest.TestCase):
    def setUp(self):
        self.complete = physical_fixture()

    def test_strict_order_accepts_two_distinct_original_boots(self):
        uart.verify_two_boots(self.complete)

    def test_single_boot_is_not_reset(self):
        tail = self.complete.split(uart.RESET_MARKER, 1)[0]
        with self.assertRaisesRegex(RuntimeError, "reset"):
            uart.verify_two_boots(tail)

    def test_forged_reset_without_physical_second_init_rejected(self):
        first = self.complete.split(uart.RESET_MARKER, 1)[0]
        with self.assertRaisesRegex(RuntimeError, "M191 ROOT"):
            uart.verify_two_boots(first + uart.RESET_MARKER + "\n"
                                  + "M242_SECOND_BOOT_NONCE=" + uart.NONCE)

    def test_reset_before_clean_ext4_flush_rejected(self):
        first = self.complete.replace(
            "[LWEXT4 GUEST] clean upstream unmount and durable flush\n"
            + uart.RESET_MARKER,
            uart.RESET_MARKER + "\n"
            + "[LWEXT4 GUEST] clean upstream unmount and durable flush", 1)
        with self.assertRaisesRegex(RuntimeError, "reset"):
            uart.verify_two_boots(first)

    def test_second_boot_must_read_actual_nonce(self):
        with self.assertRaisesRegex(RuntimeError, "M242_SECOND_BOOT_NONCE"):
            uart.verify_two_boots(self.complete.replace(
                "M242_SECOND_BOOT_NONCE=" + uart.NONCE,
                "M242_SECOND_BOOT_NONCE=" + uart.NONCE + "_BROKEN"))

    def test_final_guest_poweroff_mandatory(self):
        with self.assertRaisesRegex(RuntimeError, "/sbin/poweroff"):
            uart.verify_two_boots(self.complete.replace(
                "[EXEC] replaced process with lwext4 ELF: /sbin/poweroff", ""))

    def test_second_original_shell_mandatory(self):
        with self.assertRaisesRegex(RuntimeError, "BOTH Alpine boots"):
            text = self.complete.replace(
                "[EXEC] replaced process with lwext4 ELF: /bin/sh", "", 1)
            uart.verify_two_boots(text)

    def test_guest_failure_never_passes(self):
        with self.assertRaisesRegex(RuntimeError, "failure"):
            uart.verify_two_boots(self.complete + "\n[FAIL] fatal post-boot condition")

    def test_same_disk_must_have_exactly_one_qemu_pid(self):
        with mock.patch.object(uart.m194, "image_qemu_pids", return_value=[125, 126]):
            with self.assertRaisesRegex(RuntimeError, "exactly one"):
                uart.image_qemu_pid("/real/disk.img")

    def test_same_qemu_pid_unchanged_between_boots(self):
        with mock.patch.object(uart.m194, "image_qemu_pids", return_value=[126]):
            with self.assertRaisesRegex(RuntimeError, "PID changed across reset"):
                uart.image_qemu_pid("/real/disk.img", expected=125)
            self.assertEqual(uart.image_qemu_pid("/real/disk.img", expected=126), 126)

    def test_reset_marker_is_single_line(self):
        with mock.patch.dict("os.environ", {"NAS_M242_RESET_MARKER": "A\nB"}):
            with self.assertRaisesRegex(ValueError, "one nonempty"):
                uart.reboot_marker()

    def test_standalone_m242_sender_remains_original_by_default(self):
        self.assertIsNone(inspect.signature(uart.run).parameters["uart_send"].default)
        self.assertIsNot(uart.send_paced_uart, uart.m193.sent)

    def test_opt_in_uart_sender_preserves_every_ascii_byte_and_pace(self):
        value = "echo M244_REAL_RC=0\r"
        with mock.patch.object(uart.os, "write", return_value=1) as write, \
             mock.patch.object(uart.time, "sleep") as sleep:
            uart.send_paced_uart(42, value, seconds_per_byte=0.005)
        self.assertEqual(write.call_args_list,
                         [mock.call(42, bytes((byte,))) for byte in value.encode("ascii")])
        self.assertEqual(sleep.call_args_list,
                         [mock.call(0.005)] * len(value.encode("ascii")))

    def test_paced_sender_accepts_terminal_dsr_reply(self):
        reply = "\x1b[1;1R"
        with mock.patch.object(uart.os, "write", return_value=1) as write, \
             mock.patch.object(uart.time, "sleep"):
            uart.send_paced_uart(42, reply, seconds_per_byte=0.005)
        self.assertEqual(write.call_count, len(reply))

    def test_paced_sender_rejects_unsafe_pacing_without_writing(self):
        for interval in (0, 0.003, 0.0181, -1, float("inf"), float("nan")):
            with self.subTest(interval=interval), \
                 mock.patch.object(uart.os, "write") as write:
                with self.assertRaisesRegex(ValueError, "safety bounds"):
                    uart.send_paced_uart(42, "ok\r", seconds_per_byte=interval)
                write.assert_not_called()

    def test_paced_sender_rejects_non_ascii_or_noncanonical_line(self):
        for value in ("", "\u4e2d\r", "hello\nworld\r", "hello\x00world\r",
                      "first\rsecond\r", "first\rsecond",
                      "x" * 2049 + "\r"):
            with self.subTest(value=value[:32]), \
                 mock.patch.object(uart.os, "write") as write:
                with self.assertRaises((ValueError, UnicodeEncodeError)):
                    uart.send_paced_uart(42, value, seconds_per_byte=0.005)
                write.assert_not_called()

    def test_paced_sender_short_fifo_write_fails_closed(self):
        with mock.patch.object(uart.os, "write", return_value=0) as write, \
             mock.patch.object(uart.time, "sleep") as sleep:
            with self.assertRaisesRegex(OSError, "short UART"):
                uart.send_paced_uart(42, "safe\r", seconds_per_byte=0.005)
        write.assert_called_once_with(42, b"s")
        sleep.assert_not_called()


if __name__ == "__main__":
    unittest.main()
