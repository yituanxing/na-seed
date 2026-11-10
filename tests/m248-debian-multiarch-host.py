#!/usr/bin/env python3
"""Offline negative tests for M248's authenticated multiarch Debian fixture.

No network, APT update, dpkg extraction, or guest QEMU is invoked here.
Native guest execution is a separate mandatory acceptance gate.
"""

import hashlib
import importlib.util
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parent.parent / "scripts/m248-fetch-debian-multiarch.py"
spec = importlib.util.spec_from_file_location("m248_signed_fetcher", SCRIPT)
assert spec and spec.loader
m248 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m248)


class SignedDebianMultiarchTests(unittest.TestCase):
    def setUp(self):
        self.temp = TemporaryDirectory(prefix="m248-host-tests-")
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)

    def put(self, name, data=b"original-signed-package-elf"):
        target = self.base / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        return target

    def make_sysroot(self, arch):
        root = self.base / arch
        triplet, _, interp = m248.PORTS[arch]
        for name in m248.PROGRAMS:
            self.put(f"{arch}/usr/bin/{name}")
        self.put(f"{arch}/usr/lib/{triplet}/libc.so.6")
        self.put(f"{arch}/usr/lib/{triplet}/{Path(interp).name}")
        return root

    def verify_with_readelf(self, arch, *, machine=None, needed="libc.so.6", interp=None):
        _, canonical_machine, canonical_interp = m248.PORTS[arch]
        machine = canonical_machine if machine is None else machine
        interp = canonical_interp if interp is None else interp

        def fake_readelf(*args, capture=False):
            self.assertTrue(capture)
            self.assertEqual(args[0], "readelf")
            if args[1] == "-h":
                return f"Class: ELF64\nType: DYN (Position-Independent Executable file)\nMachine: {machine}\n"
            if args[1] == "-d":
                return "Dynamic section at offset ...\n" + "".join(
                    f" 0x1 (NEEDED) Shared library: [{item}]\n"
                    for item in needed.split("|"))
            raise AssertionError(args)

        with (patch.object(m248.m245, "run", side_effect=fake_readelf),
              patch.object(m248.m245, "elf_interpreter", return_value=interp)):
            return m248.validate_sysroot(self.base / arch, arch)

    def test_all_three_exact_official_trixie_version_pins(self):
        for arch in m248.PORTS:
            with self.subTest(arch=arch):
                self.assertEqual(m248.pinned_versions(m248.PINS, arch), {
                    "dash": "0.5.12-12", "coreutils": "9.7-3", "libc6": "2.41-12+deb13u4"})

    def test_duplicate_pin_rejected(self):
        pins = self.put("versions.tsv", b"arm64\tdash\t1\narm64\tdash\t1\narm64\tlibc6\t2\narm64\tcoreutils\t3\n")
        with self.assertRaisesRegex(ValueError, "duplicate"):
            m248.pinned_versions(pins, "arm64")

    def test_missing_pin_rejected(self):
        pins = self.put("versions.tsv", b"riscv64\tdash\t1\nriscv64\tlibc6\t2\n")
        with self.assertRaisesRegex(ValueError, "missing exact"):
            m248.pinned_versions(pins, "riscv64")

    def test_unreviewed_or_malformed_pin_rejected(self):
        for line in (b"arm64\tlibssl3\t1\n", b"riscv64\tdash\t../debian\n",
                     b"unknown\tdash\t1\n", b"arm64 dash 1\n"):
            pins = self.put("versions.tsv", line)
            with self.subTest(line=line), self.assertRaises(ValueError):
                m248.pinned_versions(pins, "arm64")

    def test_missing_or_forged_inrelease_rejected(self):
        apt = self.base / "apt"
        (apt / "lists").mkdir(parents=True)
        with self.assertRaisesRegex(ValueError, "missing/ambiguous"):
            m248.authenticated_inrelease(apt)
        self.put("apt/lists/deb.debian.org_debian_dists_trixie_InRelease", b"not-clearsigned")
        with self.assertRaisesRegex(ValueError, "not a clearsigned"):
            m248.authenticated_inrelease(apt)
        self.put("apt/lists/deb.debian.org_debian_dists_trixie_InRelease",
                 b"-----BEGIN PGP SIGNED MESSAGE-----\n")
        self.put("apt/lists/duplicate_dists_trixie_InRelease",
                 b"-----BEGIN PGP SIGNED MESSAGE-----\n")
        with self.assertRaisesRegex(ValueError, "missing/ambiguous"):
            m248.authenticated_inrelease(apt)

    def test_apt_uses_trust_enforcement_options(self):
        options = m248.m245.apt_options(self.base / "apt", arch="riscv64")
        joined = " ".join(options)
        for term in ("APT::Architecture=riscv64", "APT::Architectures=riscv64",
                     "APT::Get::AllowUnauthenticated=false",
                     "Acquire::AllowInsecureRepositories=false",
                     "Acquire::AllowDowngradeToInsecureRepositories=false"):
            self.assertIn(term, joined)

    def test_signed_packages_index_exact_sha_and_arch(self):
        digest = "a" * 64
        record = (f"Package: dash\nVersion: 0.5.12-12\nArchitecture: riscv64\n"
                  f"SHA256: {digest}\nSize: 11\nFilename: pool/main/d/dash/dash_0.5.12-12_riscv64.deb\n")
        result = m248.m245.parse_apt_show(record, package="dash", version="0.5.12-12", arch="riscv64")
        self.assertEqual(result["SHA256"], digest)
        for mutant in (record.replace("Architecture: riscv64", "Architecture: arm64"),
                       record.replace(digest, "notsha256"),
                       record.replace("Size: 11", "Size: 0"),
                       record.replace("pool/main/d/dash/", "../"),
                       record + "\n" + record):
            with self.subTest(mutant=mutant[:50]), self.assertRaises(ValueError):
                m248.m245.parse_apt_show(mutant, package="dash", version="0.5.12-12", arch="riscv64")

    def test_corrupted_cached_deb_rejected_before_network(self):
        artifact = self.put("dash.deb", b"tampered")
        record = {"SHA256": hashlib.sha256(b"authentic").hexdigest(), "Size": "9",
                  "Filename": "pool/main/d/dash/dash.deb"}
        with self.assertRaisesRegex(ValueError, "not the signed-index object"):
            m248.m245.fetch_verified_deb(record, artifact)

    def test_validate_all_three_arch_elf_and_interpreter(self):
        for arch in m248.PORTS:
            with self.subTest(arch=arch):
                self.make_sysroot(arch)
                result = self.verify_with_readelf(arch)
                self.assertEqual(result["program_interpreter"], m248.PORTS[arch][2])
                self.assertEqual(set(result["program_dt_needed"]), set(m248.PROGRAMS))
                self.assertEqual(len(result["elf_sha256"]), 7)

    def test_wrong_isa_elf_rejected(self):
        self.make_sysroot("arm64")
        with self.assertRaisesRegex(ValueError, "machine/class"):
            self.verify_with_readelf("arm64", machine="RISC-V")

    def test_wrong_pt_interp_rejected(self):
        self.make_sysroot("riscv64")
        with self.assertRaisesRegex(ValueError, "PT_INTERP"):
            self.verify_with_readelf("riscv64", interp="/lib/ld-musl-riscv64.so.1")

    def test_extra_needed_library_rejected(self):
        self.make_sysroot("amd64")
        with self.assertRaisesRegex(ValueError, "DT_NEEDED"):
            self.verify_with_readelf("amd64", needed="libselinux.so.1")

    def test_actual_foreign_glibc_loader_needed_is_strictly_allowed(self):
        for arch in ("arm64", "riscv64"):
            with self.subTest(arch=arch):
                self.make_sysroot(arch)
                loader = Path(m248.PORTS[arch][2]).name
                info = self.verify_with_readelf(
                    arch, needed=f"libc.so.6|{loader}")
                self.assertEqual(info["program_dt_needed"]["dash"]["dt_needed"],
                                 ["libc.so.6", loader])
                with self.assertRaisesRegex(ValueError, "DT_NEEDED"):
                    self.verify_with_readelf(
                        arch, needed=f"libc.so.6|{loader}|libselinux.so.1")

    def test_missing_loader_rejected(self):
        self.make_sysroot("arm64")
        triplet, _, interp = m248.PORTS["arm64"]
        (self.base / "arm64/usr/lib" / triplet / Path(interp).name).unlink()
        with self.assertRaisesRegex(ValueError, "missing/unsafe"):
            self.verify_with_readelf("arm64")

    def test_escaped_symlink_cannot_enter_sysroot(self):
        self.make_sysroot("riscv64")
        executable = self.base / "riscv64/usr/bin/dash"
        executable.unlink()
        executable.symlink_to(self.put("foreign-elf"))
        with self.assertRaisesRegex(ValueError, "missing/unsafe"):
            self.verify_with_readelf("riscv64")


if __name__ == "__main__":
    unittest.main(verbosity=2)
