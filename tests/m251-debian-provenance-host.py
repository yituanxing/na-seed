#!/usr/bin/env python3
"""Offline M251 negative provenance and real ext4 oracle contract tests."""

import hashlib
import importlib.util
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch


HERE = Path(__file__).resolve().parent.parent / "scripts"


def import_script(name: str, file: str):
    spec = importlib.util.spec_from_file_location(name, HERE / file)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


m251 = import_script("m251_provenance_offline_tests", "m251-debian-provenance.py")
disk = import_script("m251_disk_offline_tests", "m251-debian-ext4-disk.py")


class M251SignedProvenanceTests(unittest.TestCase):
    def setUp(self):
        self.work = TemporaryDirectory(prefix="m251-signed-host-test-")
        self.addCleanup(self.work.cleanup)
        self.root = Path(self.work.name)

    def put(self, relative: str, data: bytes) -> Path:
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def fake_manifest(self, port="amd64"):
        manifest = self.put("source/manifest.json", b"{\"M248\":\"preserved\"}\n")
        source = self.root / "source/sysroot"
        apt = self.root / "source/apt"
        source.mkdir()
        apt.mkdir()
        base = {
            "inrelease_sha256": "a" * 64,
            "packages": {"coreutils": {"version": "9.7-3", "sha256": "b" * 64,
                                         "size": 100, "pool": "pool/main/c/coreutils/coreutils.deb"}},
        }
        program = {}
        files = {}
        for name in m251.PROGRAMS:
            rel = f"usr/bin/{name}"
            payload = f"signed-{port}-{name}".encode()
            self.put("guest/" + rel, payload)
            self.put("source/sysroot/" + rel, payload)
            digest = hashlib.sha256(payload).hexdigest()
            program[rel] = {"sha256": digest, "interpreter": m251.m248.PORTS[port][2],
                            "dt_needed": ["libc.so.6"]}
            files[rel] = digest
        extra = {
            "schema": m251.SCHEMA,
            "architecture": port,
            "status": "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS",
            "inrelease_sha256": base["inrelease_sha256"],
            "base_manifest_sha256": m251.m245.sha256_file(manifest),
            "coreutils_package": base["packages"]["coreutils"],
            "programs": program,
            "libraries": {}, "dependency_packages": {}, "elf_sha256": files,
        }
        return manifest, source, apt, base, extra

    def verify_with_mock_elf(self, extra):
        return patch.object(m251, "elf_properties",
                            side_effect=lambda _p, port, executable: {
                                "dt_needed": ["libc.so.6"],
                                **({"interpreter": m251.m248.PORTS[port][2]} if executable else {}),
                            })

    def test_unsigned_or_unreviewed_dt_needed_fails_closed(self):
        binary = self.put("unreviewed", b"test")
        machine = m251.m248.PORTS["amd64"][1]
        header = f"Class: ELF64\nType: DYN (Shared object file)\nMachine: {machine}\n"
        with patch.object(m251.m245, "run", side_effect=[
                header, " 0x1 (NEEDED) Shared library: [libbad.so.8]\n"]):
            with self.assertRaisesRegex(ValueError, "unreviewed DT_NEEDED"):
                m251.elf_properties(binary, "amd64", executable=False)

    def test_unknown_dependency_rejected_before_download(self):
        self.assertEqual(set(m251.LIBRARY_PACKAGES),
                         {"libselinux.so.1", "libcap.so.2", "libpcre2-8.so.0",
                          "libacl.so.1", "libattr.so.1", "libgmp.so.10"})
        self.assertNotIn("libbad.so.8", m251.LIBRARY_PACKAGES)

    def test_signed_candidate_version_from_apt_authenticated_index(self):
        policy = "libselinux1:\n  Installed: (none)\n  Candidate: 3.8.1-1\n"
        record = ("Package: libselinux1\nVersion: 3.8.1-1\nArchitecture: arm64\n"
                  "SHA256: " + "c" * 64 + "\nSize: 144\n"
                  "Filename: pool/main/libs/libselinux/libselinux1_3.8.1-1_arm64.deb\n")
        with patch.object(m251.m245, "run", side_effect=[policy, record]) as execute:
            parsed = m251.signed_dependency_record(self.root / "apt", "arm64", "libselinux1")
        self.assertEqual(parsed["Version"], "3.8.1-1")
        self.assertEqual(parsed["SHA256"], "c" * 64)
        self.assertIn("APT::Get::AllowUnauthenticated=false", " ".join(execute.call_args_list[0].args))

    def test_unavailable_candidate_rejected(self):
        with patch.object(m251.m245, "run", return_value="libselinux1:\n Candidate: (none)\n"):
            with self.assertRaisesRegex(ValueError, "no authenticated"):
                m251.signed_dependency_record(self.root / "apt", "amd64", "libselinux1")

    def test_signed_guest_source_rechecks_each_program_and_rejects_tamper(self):
        manifest, source, apt, base, extra = self.fake_manifest()
        with (patch.object(m251, "source_context", return_value=(source, apt, base)),
              self.verify_with_mock_elf(extra)):
            m251.verify_root("amd64", self.root / "guest", manifest, extra)
            self.put("guest/usr/bin/ls", b"LOCAL-FAKE-LS")
            with self.assertRaisesRegex(ValueError, "guest root original ELF bytes changed"):
                m251.verify_root("amd64", self.root / "guest", manifest, extra)

    def test_complete_dependency_closure_required(self):
        manifest, source, apt, base, extra = self.fake_manifest()

        def need_selinux(path, port, *, executable):
            info = {"dt_needed": ["libc.so.6", "libselinux.so.1"]}
            if executable:
                info["interpreter"] = m251.m248.PORTS[port][2]
            return info

        for value in extra["programs"].values():
            value["dt_needed"] = ["libc.so.6", "libselinux.so.1"]
        with (patch.object(m251, "source_context", return_value=(source, apt, base)),
              patch.object(m251, "elf_properties", side_effect=need_selinux)):
            with self.assertRaisesRegex(ValueError, "incomplete or unexpected authenticated DSO closure"):
                m251.verify_root("amd64", self.root / "guest", manifest, extra)

    def test_wrong_source_manifest_sha_rejected(self):
        manifest, source, apt, base, extra = self.fake_manifest()
        extra["base_manifest_sha256"] = "0" * 64
        with patch.object(m251, "source_context", return_value=(source, apt, base)):
            with self.assertRaisesRegex(ValueError, "provenance differs"):
                m251.verify_root("amd64", self.root / "guest", manifest, extra)


class M251PhysicalDiskOracleTests(unittest.TestCase):
    def setUp(self):
        self.work = TemporaryDirectory(prefix="m251-physical-host-test-")
        self.addCleanup(self.work.cleanup)
        self.base = Path(self.work.name)
        self.root = self.base / "guest"
        self.root.mkdir()
        for rel in (*disk.m249.READBACK, disk.m250.CTTY, "etc/m250-input", *disk.READBACK):
            path = self.root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(f"original::{rel}".encode())
        (self.root / "etc/m251-init.sh").chmod(0o755)
        self.extra = {"elf_sha256": {}, "libraries": {}}

    def verify_with_marker(self, marker, *, after):
        def debugfs_cat(_part, filename):
            rel = filename.removeprefix("/")
            if rel == disk.PERSISTED_PATH:
                return marker
            return (self.root / rel).read_bytes()

        with (patch.object(disk.m249, "validate"),
              patch.object(disk.m250, "verified_launcher", return_value="c" * 64),
              patch.object(disk.provenance, "verify_root", return_value=self.extra),
              patch.object(disk.m191, "check_mbr"),
              patch.object(disk.m191, "extract_partition"),
              patch.object(disk.m191, "check_fsck"),
              patch.object(disk.m191, "debugfs_cat", side_effect=debugfs_cat)):
            disk.verify("amd64", self.root, self.base / "disk.img",
                        self.base / "manifest.json", self.base / "extra.json", after)

    def test_guest_persisted_value_requires_real_post_qemu_readback(self):
        self.verify_with_marker(b"", after=False)
        self.verify_with_marker(disk.PERSISTED, after=True)
        with self.assertRaisesRegex(ValueError, "did not persist"):
            self.verify_with_marker(b"", after=True)
        with self.assertRaisesRegex(ValueError, "already contains"):
            self.verify_with_marker(disk.PERSISTED, after=False)


if __name__ == "__main__":
    unittest.main(verbosity=2)
