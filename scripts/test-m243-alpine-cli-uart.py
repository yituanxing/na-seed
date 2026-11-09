#!/usr/bin/env python3
"""Strict host-only fault injections for the M243 same-QEMU Alpine oracle."""

import importlib.util
from pathlib import Path
import unittest
from unittest import mock


source = Path(__file__).with_name("m243-alpine-cli-uart.py")
spec = importlib.util.spec_from_file_location("naseed_m243_uart_test", source)
assert spec and spec.loader
uart = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uart)


def physical_fixture() -> str:
    first = (
        *uart.m242.BOOT,
        "[EXEC] replaced process with lwext4 ELF: /bin/sh",
        *uart.FIRST_MARKERS,
        "[EXEC] replaced process with lwext4 ELF: /sbin/reboot",
        *uart.m242.FLUSH,
        "[ALPINE REBOOT] original BusyBox; durable ext4 unmount",
        uart.m242.RESET_MARKER,
    )
    second = (
        *uart.m242.BOOT,
        "[EXEC] replaced process with lwext4 ELF: /bin/sh",
        *uart.SECOND_MARKERS,
        "[EXEC] replaced process with lwext4 ELF: /sbin/poweroff",
        *uart.m242.FLUSH,
        "[ALPINE POWER_OFF] original BusyBox; durable ext4 unmount",
        "[PASS] cmdline userspace exit 0",
    )
    return "\n".join((*first, *second))


class OriginalAlpineM243Tests(unittest.TestCase):
    def setUp(self) -> None:
        self.complete = physical_fixture()

    def test_full_original_apk_openrc_two_boot_chain_accepted(self):
        uart.verify_two_boots(self.complete)

    def test_original_public_apk_command_is_mandatory(self):
        self.assertTrue(any("apk --no-cache add tree jq" in item[0]
                            for item in uart.FIRST_COMMANDS))
        self.assertTrue(any("apk info -e tree" in item[0]
                            for item in uart.SECOND_COMMANDS))
        self.assertTrue(any("/usr/bin/jq" in item[0]
                            for item in uart.SECOND_COMMANDS))

    def test_second_openrc_must_auto_start_without_guest_start_command(self):
        self.assertTrue(any("rc-update add crond default" in item[0]
                            for item in uart.FIRST_COMMANDS))
        self.assertTrue(any("pidof crond" in item[0]
                            for item in uart.SECOND_COMMANDS))
        self.assertFalse(any("rc-service crond start" in item[0] or
                             "rc-update add crond" in item[0]
                             for item in uart.SECOND_COMMANDS))

    def test_second_boot_required_not_two_commands_same_first_boot(self):
        with self.assertRaisesRegex(RuntimeError, "M242 RESET"):
            uart.verify_two_boots(
                self.complete.replace(uart.m242.RESET_MARKER, "", 1))

    def test_package_preexisting_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "M243_PACKAGES_ABSENT"):
            uart.verify_two_boots(
                self.complete.replace("M243_PACKAGES_ABSENT",
                                      "M243_PACKAGES_PREEXIST"))

    def test_missing_public_apk_transaction_fails(self):
        with self.assertRaisesRegex(RuntimeError, "M243_PUBLIC_APK_RC"):
            uart.verify_two_boots(
                self.complete.replace("M243_PUBLIC_APK_RC=0",
                                      "M243_PUBLIC_APK_RC=1"))

    def test_mismatched_second_boot_dynamic_elf_fails(self):
        with self.assertRaisesRegex(RuntimeError, "M243_SECOND_APK_ELF_RC"):
            uart.verify_two_boots(
                self.complete.replace("M243_SECOND_APK_ELF_RC=0",
                                      "M243_SECOND_APK_ELF_RC=127"))

    def test_no_original_crond_autostart_fails(self):
        with self.assertRaisesRegex(RuntimeError, "M243_SECOND_CROND_AUTO_RC"):
            uart.verify_two_boots(
                self.complete.replace("M243_SECOND_CROND_AUTO_RC=0",
                                      "M243_SECOND_CROND_AUTO_RC=1"))

    def test_stale_first_runlevel_marker_cannot_satisfy_second(self):
        first, second = self.complete.split(uart.m242.RESET_MARKER, 1)
        changed = second.replace("M243_SECOND_CROND_AUTO_RC=0", "")
        with self.assertRaisesRegex(RuntimeError, "M243_SECOND_CROND_AUTO_RC"):
            uart.verify_two_boots(
                first + "\nM243_SECOND_CROND_AUTO_RC=0\n" +
                uart.m242.RESET_MARKER + changed)

    def test_guest_nonce_must_match_exactly_on_second_boot(self):
        with self.assertRaisesRegex(RuntimeError, "M243_SECOND_EXT4_NONCE"):
            uart.verify_two_boots(
                self.complete.replace(
                    "M243_SECOND_EXT4_NONCE=" + uart.NONCE,
                    "M243_SECOND_EXT4_NONCE=" + uart.NONCE + "_FORGED"))

    def test_guest_output_marker_must_be_whole_line(self):
        with self.assertRaisesRegex(RuntimeError, "M243_SECOND_APK_DB_RC"):
            uart.verify_two_boots(
                self.complete.replace(
                    "M243_SECOND_APK_DB_RC=0",
                    "shell> M243_SECOND_APK_DB_RC=0"))

    def test_reboot_before_clean_ext4_unmount_cannot_pass(self):
        text = self.complete.replace(
            "[LWEXT4 GUEST] clean upstream unmount and durable flush\n"
            "[ALPINE REBOOT] original BusyBox; durable ext4 unmount",
            "[ALPINE REBOOT] original BusyBox; durable ext4 unmount\n"
            "[LWEXT4 GUEST] clean upstream unmount and durable flush", 1)
        with self.assertRaisesRegex(RuntimeError, "ALPINE REBOOT"):
            uart.verify_two_boots(text)

    def test_missing_second_original_poweroff_fails(self):
        with self.assertRaisesRegex(RuntimeError, "/sbin/poweroff"):
            uart.verify_two_boots(self.complete.replace(
                "[EXEC] replaced process with lwext4 ELF: /sbin/poweroff", ""))

    def test_missing_second_original_ash_fails(self):
        first, second = self.complete.split(uart.m242.RESET_MARKER, 1)
        with self.assertRaisesRegex(RuntimeError, "BOTH boots"):
            uart.verify_two_boots(first + uart.m242.RESET_MARKER +
                                  second.replace(
                                      "[EXEC] replaced process with lwext4 ELF: /bin/sh",
                                      "", 1))

    def test_same_native_qemu_pid_is_inherited_from_m242(self):
        with mock.patch.object(uart.m242.m194, "image_qemu_pids", return_value=[701]):
            self.assertEqual(uart.m242.image_qemu_pid("/one/disk", expected=701), 701)
            with self.assertRaisesRegex(RuntimeError, "PID changed"):
                uart.m242.image_qemu_pid("/one/disk", expected=700)

    def test_guest_failure_is_never_accepted(self):
        with self.assertRaisesRegex(RuntimeError, "failure"):
            uart.verify_two_boots(self.complete + "\n[FAIL] physical ext4 error")


if __name__ == "__main__":
    unittest.main()
