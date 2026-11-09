#!/usr/bin/env python3
"""Strict host-only failure injections for headless two-boot Alpine SSH."""

import importlib.util
from pathlib import Path
import unittest
from unittest import mock


source = Path(__file__).with_name("m244-alpine-ssh-uart.py")
spec = importlib.util.spec_from_file_location("naseed_m244_uart_test", source)
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


class OriginalAlpineM244HostSSHTests(unittest.TestCase):
    def setUp(self) -> None:
        self.complete = physical_fixture()

    def test_complete_original_ssh_before_and_after_one_reset(self):
        uart.verify_two_boots(self.complete)

    def test_retains_every_original_public_apk_crond_m243_marker(self):
        self.assertEqual(uart.FIRST_MARKERS[:len(uart.m243.FIRST_MARKERS)],
                         uart.m243.FIRST_MARKERS)
        self.assertEqual(uart.SECOND_MARKERS[:len(uart.m243.SECOND_MARKERS)],
                         uart.m243.SECOND_MARKERS)

    def test_first_dropbear_must_be_upstream_signed_and_absent_preinstall(self):
        combined = "\n".join(item[0] for item in uart.FIRST_COMMANDS)
        self.assertIn("M244_DROPBEAR_ABSENT", combined)
        self.assertIn("apk --no-cache add dropbear dropbear-openrc", combined)
        self.assertIn("/usr/sbin/dropbear", combined)

    def test_original_dropbear_openrc_not_manually_started_on_reboot(self):
        first = "\n".join(item[0] for item in uart.FIRST_COMMANDS)
        second = "\n".join(item[0] for item in uart.SECOND_COMMANDS)
        self.assertIn("rc-update add dropbear default", first)
        self.assertIn("rc-service dropbear start", first)
        self.assertIn("rc-service dropbear status", second)
        self.assertNotIn("rc-service dropbear start", second)
        self.assertNotIn("rc-update add dropbear", second)

    def test_original_ifupdown_networking_dependency_before_dropbear(self):
        first = "\n".join(item[0] for item in uart.FIRST_COMMANDS)
        self.assertIn("/sbin/ifquery -i /etc/network/interfaces --list -a", first)
        self.assertIn("pre-up /bin/sh /root/m244-network-ready.sh", first)
        self.assertIn("rc-update add networking boot", first)
        self.assertIn("rc-service networking start", first)
        self.assertLess(first.index("rc-service networking start"),
                        first.index("rc-service dropbear start"))

    def test_networking_auto_recovers_on_second_openrc_boot(self):
        second = "\n".join(item[0] for item in uart.SECOND_COMMANDS)
        self.assertIn("rc-service networking status", second)
        self.assertIn("/proc/net/naseed-resolv.conf", second)
        self.assertNotIn("rc-service networking start", second)

    def test_missing_first_original_networking_start_fails(self):
        with self.assertRaisesRegex(RuntimeError, "M244_FIRST_NETWORKING_RC"):
            uart.verify_two_boots(self.complete.replace("M244_FIRST_NETWORKING_RC=0", ""))

    def test_only_one_guest_signed_ed25519_pubkey_and_no_pty_ban(self):
        self.assertIn("ssh-ed25519", "\n".join(item[0] for item in uart.FIRST_COMMANDS))
        self.assertEqual(uart.KEY_POLICY,
                         "no-port-forwarding,no-agent-forwarding,no-X11-forwarding")
        self.assertNotIn("no-pty", uart.KEY_POLICY)
        self.assertIn("-s -g -j -k -m", uart.POLICY)

    def test_no_historical_m240_fixed_300sec_sleep(self):
        commands = "\n".join(item[0] for item in uart.FIRST_COMMANDS +
                             uart.SECOND_COMMANDS)
        self.assertNotIn("sleep 300", commands)
        self.assertNotIn("m227-alpine-openrc-workload", commands)
        self.assertIn('"$n" -lt 220', commands)

    def test_first_receipt_must_precede_native_reboot(self):
        with self.assertRaisesRegex(RuntimeError, "M244_FIRST_HOST_PTY_RC"):
            uart.verify_two_boots(self.complete.replace("M244_FIRST_HOST_PTY_RC=0", ""))

    def test_second_receipt_must_precede_native_poweroff(self):
        with self.assertRaisesRegex(RuntimeError, "M244_SECOND_HOST_PTY_RC"):
            uart.verify_two_boots(self.complete.replace("M244_SECOND_HOST_PTY_RC=0", ""))

    def test_first_ssh_must_be_real_original_guest_ready(self):
        with self.assertRaisesRegex(RuntimeError, "M244_FIRST_HOST_SSH_READY"):
            uart.verify_two_boots(self.complete.replace("M244_FIRST_HOST_SSH_READY", ""))

    def test_second_ssh_must_be_real_original_guest_ready(self):
        with self.assertRaisesRegex(RuntimeError, "M244_SECOND_HOST_SSH_READY"):
            uart.verify_two_boots(self.complete.replace("M244_SECOND_HOST_SSH_READY", ""))

    def test_guest_preseeded_ssh_identity_fails(self):
        with self.assertRaisesRegex(RuntimeError, "M244_UNSEEDED_SSH_STATE_RC"):
            uart.verify_two_boots(self.complete.replace("M244_UNSEEDED_SSH_STATE_RC=0",
                                                        "M244_UNSEEDED_SSH_STATE_RC=1"))

    def test_guest_package_install_failure_fails(self):
        with self.assertRaisesRegex(RuntimeError, "M244_SIGNED_DROPBEAR_APK_RC"):
            uart.verify_two_boots(self.complete.replace("M244_SIGNED_DROPBEAR_APK_RC=0",
                                                        "M244_SIGNED_DROPBEAR_APK_RC=1"))

    def test_reboot_fails_to_restart_daemon(self):
        with self.assertRaisesRegex(RuntimeError, "M244_SECOND_DROPBEAR_AUTO_RC"):
            uart.verify_two_boots(self.complete.replace("M244_SECOND_DROPBEAR_AUTO_RC=0",
                                                        "M244_SECOND_DROPBEAR_AUTO_RC=1"))

    def test_host_ed25519_key_changed_after_reboot(self):
        with self.assertRaisesRegex(RuntimeError, "M244_SECOND_HOSTKEY_RC"):
            uart.verify_two_boots(self.complete.replace("M244_SECOND_HOSTKEY_RC=0",
                                                        "M244_SECOND_HOSTKEY_RC=1"))

    def test_first_epoch_cannot_satisfy_second_ssh_receipt(self):
        first, second = self.complete.split(uart.m242.RESET_MARKER, 1)
        second = second.replace("M244_SECOND_HOST_PTY_RC=0", "")
        fake = first + "\nM244_SECOND_HOST_PTY_RC=0\n" + uart.m242.RESET_MARKER + second
        with self.assertRaisesRegex(RuntimeError, "M244_SECOND_HOST_PTY_RC"):
            uart.verify_two_boots(fake)

    def test_uart_guest_marker_requires_exact_whole_line(self):
        with self.assertRaisesRegex(RuntimeError, "M244_FIRST_HOST_SSH_READY"):
            uart.verify_two_boots(self.complete.replace("M244_FIRST_HOST_SSH_READY",
                                                        "shell> M244_FIRST_HOST_SSH_READY"))

    def test_original_reboot_cannot_be_replaced_by_two_shell_commands(self):
        with self.assertRaisesRegex(RuntimeError, "M242 RESET"):
            uart.verify_two_boots(self.complete.replace(uart.m242.RESET_MARKER, ""))

    def test_second_real_alpine_ash_required(self):
        first, second = self.complete.split(uart.m242.RESET_MARKER, 1)
        changed = second.replace("[EXEC] replaced process with lwext4 ELF: /bin/sh", "", 1)
        with self.assertRaisesRegex(RuntimeError, "BOTH boots"):
            uart.verify_two_boots(first + uart.m242.RESET_MARKER + changed)

    def test_second_poweroff_required(self):
        with self.assertRaisesRegex(RuntimeError, "/sbin/poweroff"):
            uart.verify_two_boots(self.complete.replace(
                "[EXEC] replaced process with lwext4 ELF: /sbin/poweroff", ""))

    def test_qemu_pid_identity_still_owned_by_m242(self):
        with mock.patch.object(uart.m242.m194, "image_qemu_pids", return_value=[1101]):
            self.assertEqual(uart.m242.image_qemu_pid("/disk", expected=1101), 1101)
            with self.assertRaisesRegex(RuntimeError, "PID changed"):
                uart.m242.image_qemu_pid("/disk", expected=1100)


if __name__ == "__main__":
    unittest.main()
