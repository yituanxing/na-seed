#!/usr/bin/env python3
"""M249 negative contracts; no fake success marker or QEMU emulation."""

import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest


FILE = Path(__file__).resolve().parents[1] / "scripts/m249-debian-ext4-disk.py"
SPEC = importlib.util.spec_from_file_location("m249_disk_test_target", FILE)
assert SPEC and SPEC.loader
disk = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(disk)


class AuthenticatedSourceContract(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="m249-negative-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "root"
        self.root.mkdir()
        self.triple = "riscv64-linux-gnu"
        self.loader = "ld-linux-riscv64-lp64d.so.1"
        self.signed = {}
        entries = [f"usr/bin/{name}" for name in disk.PROGRAMS]
        entries += [f"usr/lib/{self.triple}/libc.so.6",
                    f"usr/lib/{self.triple}/{self.loader}"]
        for name in entries:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            data = bytearray(64)
            data[:6] = b"\x7fELF\x02\x01"
            struct.pack_into("<H", data, 18, 243)
            path.write_bytes(data)
            self.signed[name] = hashlib.sha256(data).hexdigest()
        (self.root / "lib").mkdir()
        (self.root / "lib" / self.triple).symlink_to("../usr/lib/" + self.triple)
        (self.root / "lib" / self.loader).symlink_to(
            "../usr/lib/" + self.triple + "/" + self.loader)
        for name in ("dev", "etc", "tmp", "var/tmp", "proc", "sys"):
            (self.root / name).mkdir(parents=True, exist_ok=True)
        script = self.root / "etc/m249-init.sh"
        script.write_text("#!/usr/bin/dash\nexit 0\n")
        script.chmod(0o755)
        (self.root / "etc/m249-input").write_text("M249-EXT4-INPUT\n")
        self.manifest = Path(self.temp.name) / "manifest.json"
        self.info = {
            "architecture": "riscv64",
            "distribution": "Debian 13 trixie",
            "status": "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS",
            "elf_sha256": self.signed,
        }
        self.save()

    def save(self):
        self.manifest.write_text(json.dumps(self.info))

    def valid(self):
        return disk.validate("riscv64", self.root, self.manifest)

    def test_exact_signed_elf_and_minimal_filesystem(self):
        self.assertEqual(self.valid(), self.signed)

    def test_reject_foreign_architecture(self):
        self.info["architecture"] = "arm64"
        self.save()
        with self.assertRaisesRegex(ValueError, "provenance"):
            self.valid()

    def test_reject_unverified_status(self):
        self.info["status"] = "NATIVE_QEMU_PASS"
        self.save()
        with self.assertRaisesRegex(ValueError, "provenance"):
            self.valid()

    def test_reject_wrong_elf_architecture(self):
        name = self.root / "usr/bin/dash"
        data = bytearray(name.read_bytes())
        struct.pack_into("<H", data, 18, 183)
        name.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "native ELF"):
            self.valid()

    def test_reject_signed_elf_modified(self):
        name = self.root / "usr/bin/wc"
        name.write_bytes(name.read_bytes() + b"tamper")
        with self.assertRaisesRegex(ValueError, "signed package"):
            self.valid()

    def test_reject_interpreter_link_tamper(self):
        link = self.root / "lib" / self.loader
        link.unlink()
        link.symlink_to("/host/outside")
        with self.assertRaisesRegex(ValueError, "PT_INTERP"):
            self.valid()

    def test_reject_unsafe_program_symlink(self):
        path = self.root / "usr/bin/dash"
        path.unlink()
        path.symlink_to("/bin/dash")
        with self.assertRaisesRegex(ValueError, "regular"):
            self.valid()

    def test_reject_missing_real_disk_dir(self):
        (self.root / "var/tmp").rmdir()
        with self.assertRaisesRegex(ValueError, "missing guest directory"):
            self.valid()


if __name__ == "__main__":
    unittest.main()
