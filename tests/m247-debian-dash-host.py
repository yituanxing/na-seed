#!/usr/bin/env python3
"""Offline negative contracts for M247; never assert guest success here."""

import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
FETCH = Path("scripts/m247-fetch-debian-dash.py")
spec = importlib.util.spec_from_file_location("m247_dash_fetcher", FETCH)
assert spec and spec.loader
m247 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m247)


class OriginalDebianDashContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.probe = Path("scripts/m247-probe-debian-dash-pid1.sh").read_text()
        cls.fetch = FETCH.read_text()

    def test_wrong_arch_fails_before_network(self):
        for args in ([], ["arm64"], ["amd64", "extra"]):
            with self.subTest(args=args):
                p = subprocess.run(["sh", "scripts/m247-probe-debian-dash-pid1.sh", *args],
                                   capture_output=True, timeout=5)
                self.assertEqual(p.returncode, 2)

    def test_exact_separate_dash_pin(self):
        self.assertEqual(m247.pinned_version(Path("scripts/m247-debian-dash-version.tsv")),
                         "0.5.12-12")
        with tempfile.TemporaryDirectory() as temp:
            p = Path(temp) / "wrong.tsv"
            for raw in ("amd64\tdash\t", "arm64\tdash\t0.5.12-12\n",
                        "amd64\tdash\t0.5.12-12\namd64\tdash\t0.5.12-12\n"):
                p.write_text(raw)
                with self.assertRaises(ValueError):
                    m247.pinned_version(p)

    def test_reuses_authentic_signed_index_and_sha_checked_deb(self):
        self.assertIn('m245.apt_options(m245_out / "apt"', self.fetch)
        self.assertIn("m245.parse_apt_show(", self.fetch)
        self.assertIn("m245.fetch_verified_deb(record, deb)", self.fetch)
        self.assertIn('signed.get("inrelease_sha256")', self.fetch)
        self.assertIn("verify_dash(stage / DASH_PATH)", self.fetch)
        self.assertIn("HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS", self.fetch)

    def test_original_dash_direct_pid1_and_actual_script(self):
        self.assertIn('"$case_id" cpio /etc/m247-case.sh M247-DASH-PID1-OK - /usr/bin/dash',
                      self.probe)
        self.assertIn("naseed.argv=/usr/bin/dash /etc/m247-case.sh", self.probe)
        self.assertIn("'set -eu'", self.probe)
        self.assertIn("'value=$(/usr/bin/cat /etc/m247-input)'", self.probe)
        self.assertIn("'wait \"$pipe_pid\"'", self.probe)
        self.assertIn('NAS_ACCEPTANCE_CPIO="$archive"', self.probe)
        self.assertEqual(self.probe.count("sh scripts/probe-x86-busybox-parity.sh"), 1)

    def test_real_guest_oracles_after_one_qemu(self):
        self.assertIn("[M5] CPIO executable selected: /usr/bin/dash", self.probe)
        for program in ("true", "cat", "wc", "printf"):
            self.assertIn(f"/usr/bin/{program}", self.probe)
        self.assertIn('test "$dyn_count" -ge 5', self.probe)
        self.assertIn("grep -Fxq M247-DASH-PID1-OK", self.probe)
        self.assertIn("[PASS] cmdline userspace exit 0", self.probe)
        self.assertLess(self.probe.index("sh scripts/probe-x86-busybox-parity.sh"),
                        self.probe.index("[M247 ORIGINAL DEBIAN DASH DIRECT PID1 REAL X86 QEMU PASS]"))


if __name__ == "__main__":
    unittest.main()
