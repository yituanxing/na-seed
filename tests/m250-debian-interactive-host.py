#!/usr/bin/env python3
"""Negative host contracts only; these checks DO NOT simulate a guest PASS."""

import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

BASE = Path(__file__).resolve().parents[1]


def imported(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


disk = imported(BASE / "scripts/m250-debian-ext4-disk.py", "m250_disk_tests")
uart = imported(BASE / "scripts/m250-debian-uart.py", "m250_uart_tests")


class PhysicalDiskContract(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="m250-host-negative-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "root"
        self.root.mkdir()
        triplet = "riscv64-linux-gnu"
        loader = "ld-linux-riscv64-lp64d.so.1"
        elf_names = [f"usr/bin/{name}" for name in disk.m249.PROGRAMS] + [
            f"usr/lib/{triplet}/libc.so.6", f"usr/lib/{triplet}/{loader}"]
        self.signed = {}
        for name in [*elf_names, disk.CTTY]:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            binary = bytearray(64)
            binary[:6] = b"\x7fELF\x02\x01"
            struct.pack_into("<H", binary, 18, 243)
            path.write_bytes(binary)
            path.chmod(0o755)
            if name in elf_names:
                self.signed[name] = hashlib.sha256(binary).hexdigest()
        (self.root / "lib").mkdir()
        (self.root / "lib" / triplet).symlink_to("../usr/lib/" + triplet)
        (self.root / "lib" / loader).symlink_to("../usr/lib/" + triplet + "/" + loader)
        for name in ("dev", "etc", "tmp", "var/tmp", "proc", "sys"):
            (self.root / name).mkdir(parents=True, exist_ok=True)
        (self.root / "etc/m249-init.sh").write_text("#!/usr/bin/dash\nexit 0\n")
        (self.root / "etc/m249-init.sh").chmod(0o755)
        (self.root / "etc/m249-input").write_text("M249-EXT4-INPUT\n")
        (self.root / "etc/m250-input").write_bytes(disk.INPUT)
        self.manifest = Path(self.temp.name) / "manifest.json"
        self.manifest.write_text(json.dumps({
            "architecture": "riscv64", "distribution": "Debian 13 trixie",
            "status": "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS",
            "elf_sha256": self.signed,
        }))

    def test_original_source_and_test_only_ctty_validated_separately(self):
        self.assertEqual(disk.m249.validate("riscv64", self.root, self.manifest),
                         self.signed)
        self.assertEqual(len(disk.verified_launcher("riscv64", self.root)), 64)

    def test_modified_original_debian_coreutils_refused(self):
        with (self.root / "usr/bin/wc").open("ab") as output:
            output.write(b"unsigned-change")
        with self.assertRaisesRegex(ValueError, "signed package"):
            disk.m249.validate("riscv64", self.root, self.manifest)

    def test_wrong_isa_test_only_launcher_refused(self):
        elf = self.root / disk.CTTY
        data = bytearray(elf.read_bytes())
        struct.pack_into("<H", data, 18, 183)
        elf.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "wrong native ELF"):
            disk.verified_launcher("riscv64", self.root)

    def test_missing_launcher_refused(self):
        (self.root / disk.CTTY).unlink()
        with self.assertRaisesRegex(ValueError, "CTTY helper"):
            disk.verified_launcher("riscv64", self.root)

    def test_original_baseline_input_refused_if_changed(self):
        (self.root / "etc/m250-input").write_text("tampered\n")
        with self.assertRaisesRegex(ValueError, "baseline"):
            disk.verified_launcher("riscv64", self.root)


class RealSerialContract(unittest.TestCase):
    def test_accepts_actual_guest_output_line(self):
        self.assertTrue(uart.output_line(b"echoed cmd\nM250-HELLO\n", b"M250-HELLO"))
        self.assertTrue(uart.output_line(b"cmd\n   16   \n", b"16"))

    def test_rejects_command_echo_containing_expected_text(self):
        self.assertFalse(uart.output_line(b"/usr/bin/printf '%s\\n' M250-HELLO\n",
                                          b"M250-HELLO"))
        self.assertFalse(uart.output_line(b"cat /etc/m250-input | wc -c\n", b"16"))
        self.assertFalse(uart.output_line(b"M250-PERSISTED > /var/tmp/m250-persisted\n",
                                          b"M250-PERSISTED"))

    def test_shell_steps_require_real_uart_and_guest_clean_exit(self):
        self.assertEqual(len(uart.COMMANDS), 8)
        self.assertIn(b"exit\r", [cmd for cmd, _, _ in uart.COMMANDS])
        self.assertIn(b"[LWEXT4 GUEST] clean upstream unmount and durable flush",
                      uart.REQUIRED)
        self.assertIn(b"[ARM64] userspace debug-exit status=0", uart.NATIVE_EXIT.values())

    def test_rootfs_on_disk_precedes_qemu_and_after_fsck(self):
        probe = (BASE / "scripts/m250-probe-debian-interactive.sh").read_text()
        prepare = 'sudo python3 scripts/m250-debian-ext4-disk.py prepare'
        ownership = 'sudo chown "$(id -u):$(id -g)" "$disk"'
        runner = 'python3 scripts/m250-debian-uart.py'
        after = 'sudo python3 scripts/m250-debian-ext4-disk.py after'
        self.assertLess(probe.index(prepare), probe.index(ownership))
        self.assertLess(probe.index(ownership), probe.index(runner))
        self.assertLess(probe.index(runner), probe.index(after))
        self.assertIn('export NAS_QEMU_SERIAL_FIFO="$fifo"', probe)
        self.assertIn('cpio -it --quiet < "$rescue/rootfs.cpio"', probe)


if __name__ == "__main__":
    unittest.main()
