#!/usr/bin/env python3
"""Fast offline negative contracts for M245's signed Debian dynamic ELF gate."""

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


sys.dont_write_bytecode = True
FETCH = Path("scripts/m245-fetch-debian-dynamic.py")
spec = importlib.util.spec_from_file_location("m245_fetch", FETCH)
assert spec and spec.loader
m245 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m245)


def stanza(*, package="libc6", version="2.41-12+deb13u4", arch="amd64",
           sha="a" * 64, filename="pool/main/g/glibc/libc6_2.41-12+deb13u4_amd64.deb"):
    return (f"Package: {package}\nVersion: {version}\nArchitecture: {arch}\n"
            f"Filename: {filename}\nSize: 123456\nSHA256: {sha}\n")


class SignedPackageContracts(unittest.TestCase):
    def resolve(self, raw):
        return m245.parse_apt_show(raw, package="libc6", version="2.41-12+deb13u4", arch="amd64")

    def test_exact_signed_record(self):
        item = self.resolve(stanza())
        self.assertEqual(item["SHA256"], "a" * 64)
        self.assertEqual(item["Architecture"], "amd64")

    def test_never_accept_unsigned_missing_or_wrong_arch(self):
        for text in ["", stanza(arch="arm64"), stanza(version="2.41-12+deb13u3"),
                     stanza(sha="-"), stanza(sha="A" * 64),
                     stanza(filename="https://evil.example/fake.deb"),
                     stanza(filename="pool/main/g/../../fake.deb"),
                     stanza(filename="/pool/main/g/glibc/fake.deb"),
                     stanza().replace("Size: 123456", "Size: -1"),
                     stanza() + "\n" + stanza()]:
            with self.subTest(input=text[:70]), self.assertRaises(ValueError):
                self.resolve(text)

    def test_no_weakening_of_apt_signature_policy(self):
        with tempfile.TemporaryDirectory() as temp:
            options = m245.apt_options(Path(temp), arch="amd64")
        text = " ".join(options)
        self.assertIn("APT::Get::AllowUnauthenticated=false", text)
        self.assertIn("Acquire::AllowInsecureRepositories=false", text)
        self.assertIn("Acquire::Check-Valid-Until=true", text)
        self.assertIn("Dir::State::lists=", text)

    def test_one_fixed_pair_not_any_current_package(self):
        source = Path("scripts/m245-debian-versions.tsv").read_text()
        with tempfile.TemporaryDirectory() as temp:
            config = Path(temp) / "versions.tsv"
            config.write_text(source)
            self.assertEqual(m245.expected_versions(config, "amd64"),
                             {"coreutils": "9.7-3", "libc6": "2.41-12+deb13u4"})
            config.write_text(source + "amd64\tlibc6\t0.0\n")
            with self.assertRaises(ValueError):
                m245.expected_versions(config, "amd64")

    def test_guest_marker_is_post_real_qemu_only(self):
        probe = Path("scripts/m245-probe-debian-glibc.sh").read_text()
        self.assertIn("sh scripts/probe-x86-busybox-parity.sh", probe)
        self.assertIn("[EXEC] replaced process with CPIO ELF: /usr/bin/printf", probe)
        self.assertIn("[DYN] real ELF interpreter loaded, entry=", probe)
        self.assertIn("[PASS] cmdline userspace exit 0", probe)
        self.assertLess(probe.index("sh scripts/probe-x86-busybox-parity.sh"),
                        probe.index("[M245 ORIGINAL DEBIAN DYNAMIC GLIBC REAL X86 QEMU PASS]"))
        self.assertIn("HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS", FETCH.read_text())


if __name__ == "__main__":
    unittest.main()
