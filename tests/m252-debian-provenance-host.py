#!/usr/bin/env python3
"""Offline M252 signed Debian provenance, closure and ext4 negative oracles.

No unit mock is evidence of native NaSeed guest execution.
"""

import hashlib
import importlib.util
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch


HERE = Path(__file__).resolve().parent.parent / "scripts"


def import_script(name: str, filename: str):
    spec = importlib.util.spec_from_file_location(name, HERE / filename)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


provenance = import_script("m252_provenance_host_tests", "m252-debian-provenance.py")
disk = import_script("m252_ext4_disk_host_tests", "m252-debian-ext4-disk.py")


class M252SignedHostTests(unittest.TestCase):
    def setUp(self):
        self.temp = TemporaryDirectory(prefix="m252-signed-test-")
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root = self.base / "guest"
        self.root.mkdir()
        self.parent_manifest = self.put("source/manifest.json", b"M248-source-unchanged\n")
        self.m251_manifest = self.put("source/m251.json", b"M251-source-unchanged\n")
        self.apt = self.base / "source/apt"
        self.apt.mkdir()
        self.signed = {name: f"official-debian-13-{name}".encode()
                       for name in provenance.PROGRAM_PACKAGES}
        self.records = {}
        self.programs = {}
        self.hashes = {}
        for name, package in provenance.PROGRAM_PACKAGES.items():
            relative = f"usr/bin/{name}"
            digest = hashlib.sha256(self.signed[name]).hexdigest()
            self.put("guest/" + relative, self.signed[name])
            self.programs[relative] = {
                "source_package": package, "sha256": digest,
                "interpreter": provenance.m248.PORTS["amd64"][2],
                "dt_needed": ["libc.so.6"],
            }
            self.hashes[relative] = digest
            archive = f"debian-{package}-archive".encode()
            pool = f"pool/main/{package[0]}/{package}/{package}_1-1_amd64.deb"
            self.put("source/debs/" + Path(pool).name, archive)
            self.records[package] = {
                "Package": package, "Version": "1-1", "Architecture": "amd64",
                "SHA256": hashlib.sha256(archive).hexdigest(),
                "Size": str(len(archive)), "Filename": pool,
            }
        self.inherited = {
            "status": "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS",
            "architecture": "amd64", "libraries": {},
        }
        self.base_info = {"inrelease_sha256": "a" * 64}
        self.extra = {
            "schema": provenance.SCHEMA,
            "status": "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS",
            "architecture": "amd64",
            "inrelease_sha256": self.base_info["inrelease_sha256"],
            "base_manifest_sha256": provenance.m245.sha256_file(self.parent_manifest),
            "m251_manifest_sha256": provenance.m245.sha256_file(self.m251_manifest),
            "program_packages": {name: provenance.package_summary(record)
                                 for name, record in self.records.items()},
            "dependency_packages": {},
            "programs": self.programs,
            "libraries": {}, "reused_libraries": {}, "elf_sha256": self.hashes,
        }

    def put(self, relative: str, contents: bytes) -> Path:
        path = self.base / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(contents)
        return path

    def mock_originals(self):
        def fake_run(*args, **_kwargs):
            if args[0:2] != ("dpkg-deb", "-x"):
                raise AssertionError(f"unexpected external call {args}")
            archive_name = Path(args[2]).name
            package = next((package for package, record in self.records.items()
                            if Path(record["Filename"]).name == archive_name), None)
            if package is None:
                raise AssertionError(f"unknown archive {archive_name}")
            output = Path(args[3]) / "usr/bin" / (
                "find" if package == "findutils" else package)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(self.signed[output.name])
            return ""

        def fake_elf(_path, port, *, executable):
            return {"dt_needed": ["libc.so.6"],
                    **({"interpreter": provenance.m248.PORTS[port][2]}
                       if executable else {})}

        return (patch.object(provenance, "source_context",
                             return_value=(self.base / "source/sysroot", self.apt,
                                           self.base_info, self.inherited)),
                patch.object(provenance, "package_record",
                             side_effect=lambda _apt, _port, package: self.records[package]),
                patch.object(provenance, "elf_properties", side_effect=fake_elf),
                patch.object(provenance.m245, "run", side_effect=fake_run))

    def verify(self):
        mocks = self.mock_originals()
        with mocks[0], mocks[1], mocks[2], mocks[3]:
            return provenance.verify_root("amd64", self.root, self.parent_manifest,
                                          self.m251_manifest, self.extra)

    def test_exact_original_package_mapping(self):
        self.assertEqual(provenance.PROGRAM_PACKAGES,
                         {"grep": "grep", "sed": "sed", "find": "findutils"})
        self.assertEqual(provenance.LIBRARY_PACKAGES,
                         {**provenance.m251.LIBRARY_PACKAGES,
                          "libm.so.6": "libc6"})
        self.assertEqual(set(self.verify()["programs"]),
                         {"usr/bin/grep", "usr/bin/sed", "usr/bin/find"})

    def test_unreviewed_dt_needed_rejected(self):
        machine = provenance.m248.PORTS["amd64"][1]
        header = f"Class: ELF64\nType: DYN (Shared object file)\nMachine: {machine}\n"
        with patch.object(provenance.m245, "run", side_effect=[
                header, "0x1 (NEEDED) Shared library: [libevil.so.9]\n"]):
            with self.assertRaisesRegex(ValueError, "unreviewed glibc"):
                provenance.elf_properties(self.root / "usr/bin/grep", "amd64", executable=False)

    def test_package_records_delegate_to_m251_signed_apt_lookup(self):
        with patch.object(provenance.m251, "signed_dependency_record",
                          return_value=self.records["grep"]) as signed:
            self.assertIs(provenance.package_record(self.apt, "amd64", "grep"),
                          self.records["grep"])
            signed.assert_called_once_with(self.apt, "amd64", "grep")

    def test_signed_manifest_parent_digest_tamper_rejected(self):
        self.extra["m251_manifest_sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "both signed parent manifests"):
            self.verify()

    def test_guest_binary_tamper_rejected(self):
        self.put("guest/usr/bin/grep", b"fake-local-replacement")
        with self.assertRaisesRegex(ValueError, "original Debian ELF modified"):
            self.verify()

    def test_signed_deb_sha_and_size_rechecked(self):
        self.put("source/debs/grep_1-1_amd64.deb", b"different-archive")
        with self.assertRaisesRegex(ValueError, r"signed Debian .deb package bytes changed"):
            self.verify()

    def test_staged_binary_must_equal_original_package_extraction(self):
        # Forging both the root bytes and self-consistent M252 digests does not
        # pass: verified package unpacking is an independent original oracle.
        payload = b"not-the-actual-signed-debian-grep"
        self.put("guest/usr/bin/grep", payload)
        digest = hashlib.sha256(payload).hexdigest()
        self.extra["elf_sha256"]["usr/bin/grep"] = digest
        self.extra["programs"]["usr/bin/grep"]["sha256"] = digest
        with self.assertRaisesRegex(ValueError, "not original signed Debian grep ELF"):
            self.verify()

    def test_missing_reused_signed_dso_fails_closure(self):
        triplet = provenance.m248.PORTS["amd64"][0]
        relative = f"usr/lib/{triplet}/libselinux.so.1"
        self.inherited["libraries"][relative] = {"sha256": "f" * 64}
        for value in self.extra["programs"].values():
            value["dt_needed"].append("libselinux.so.1")

        def elf_with_selinux(_path, port, *, executable):
            return {"dt_needed": ["libc.so.6", "libselinux.so.1"],
                    **({"interpreter": provenance.m248.PORTS[port][2]}
                       if executable else {})}

        mocks = self.mock_originals()
        with mocks[0], mocks[1], patch.object(provenance, "elf_properties",
                                               side_effect=elf_with_selinux):
            with self.assertRaisesRegex(ValueError, "unclosed signed dependency"):
                provenance.verify_root("amd64", self.root, self.parent_manifest,
                                       self.m251_manifest, self.extra)


class M252PhysicalDiskHostTests(unittest.TestCase):
    def setUp(self):
        self.temp = TemporaryDirectory(prefix="m252-disk-test-")
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root = self.base / "guest"
        self.root.mkdir()
        for relative in disk.READBACK:
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(f"real M252 guest input: {relative}".encode())
        (self.root / "etc/m252-init.sh").chmod(0o755)
        self.extra = {"elf_sha256": {}, "libraries": {}, "reused_libraries": {}}

    def verify_with_marker(self, marker, *, after):
        def fake_debugfs(_part, path):
            relative = path.removeprefix("/")
            if relative == disk.PERSISTED_PATH:
                return marker
            return (self.root / relative).read_bytes()

        with (patch.object(disk.m251disk, "verify") as inherited,
              patch.object(disk.provenance, "verify_root", return_value=self.extra),
              patch.object(disk.m191, "check_mbr"),
              patch.object(disk.m191, "extract_partition"),
              patch.object(disk.m191, "check_fsck"),
              patch.object(disk.m191, "debugfs_cat", side_effect=fake_debugfs)):
            disk.verify("amd64", self.root, self.base / "disk.img",
                        self.base / "m248.json", self.base / "m251.json",
                        self.base / "m252.json", after=after)
            self.assertFalse(inherited.call_args.kwargs["after"],
                             "M251 marker must never be treated as M252 success")

    def test_before_rejects_preseeded_guest_marker(self):
        self.verify_with_marker(b"", after=False)
        with self.assertRaisesRegex(ValueError, "preseeded"):
            self.verify_with_marker(disk.PERSISTED, after=False)

    def test_after_requires_exact_native_guest_readback(self):
        self.verify_with_marker(disk.PERSISTED, after=True)
        for value in (b"", b"M252-DURABLE", b"M251-DURABLE\n"):
            with self.subTest(marker=value), self.assertRaisesRegex(ValueError, "did not persist"):
                self.verify_with_marker(value, after=True)

    def test_non_executable_guest_init_rejected(self):
        (self.root / "etc/m252-init.sh").chmod(0o644)
        with self.assertRaisesRegex(ValueError, "not executable"):
            self.verify_with_marker(b"", after=False)


if __name__ == "__main__":
    unittest.main(verbosity=2)
