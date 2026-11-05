#!/usr/bin/env python3
"""Pure provenance/eligibility regression tests; no QEMU or fake APK PASS."""

import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("m204-alpine-checkpoint.py")
spec = importlib.util.spec_from_file_location("m204_checkpoint_test_target", SCRIPT)
assert spec and spec.loader
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
REPO = Path(__file__).resolve().parent.parent


class CheckpointTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="m204-checkpoint-host-")
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        self.root = self.base / "root"
        self.root.mkdir()
        self.disk = self.base / "checkpoint.img"
        # A sparse image exercises the actual physical geometry/sha guard.
        with self.disk.open("wb") as stream:
            stream.truncate(256 * 1024 * 1024)
        self.serial = self.base / "first.serial.log"
        self.reopen = self.base / "second.clean.log"
        self.pcap = self.base / "first.pcap"
        self.pcap.write_bytes(b"pcap from a separately verified cold public boot")
        self.meta = self.base / "checkpoint.json"
        self.write_serial()
        self.write_reopen()
        self.write_metadata()

    def write_serial(self, missing=None, bad_install=None):
        lines = [marker for marker in module.MARKERS if marker != missing]
        for category, package in module.package_manifest(REPO):
            state = "FAIL" if package == bad_install else "PASS"
            lines.append(f"M204_RESULT\t{category}\t{package}\tinstall\t{state}")
        self.serial.write_text("\r\n".join(lines) + "\r\n")

    def write_reopen(self, missing=None):
        lines = [marker for marker in module.REOPEN_MARKERS if marker != missing]
        for category, package in module.package_manifest(REPO):
            lines.append(f"M204_RESULT\t{category}\t{package}\treopen\tPASS")
        self.reopen.write_text("\r\n".join(lines) + "\r\n")

    def write_metadata(self):
        self.meta.write_text(json.dumps({
            "version": module.VERSION, "alpine": module.ALPINE,
            "checkpoint_stage": "post-independent-clean-reopen",
            "port": "amd64", "origin_commit": "d" * 40,
            "source_hashes": module.source_hashes(REPO),
            "packages": [package for _, package in module.package_manifest(REPO)],
            "sha256": {"disk": module.sha256(self.disk),
                       "serial": module.sha256(self.serial),
                       "pcap": module.sha256(self.pcap),
                       "reopen_serial": module.sha256(self.reopen)},
        }))

    def verify(self):
        # Real integration always checks ext4 and M197 signed APK oracle.
        # This synthetic unit tests strictly only the independent manifest,
        # evidence digests and replay eligibility checks.
        return module.verify("amd64", self.root, self.disk, self.meta,
                             self.serial, self.pcap, self.reopen, REPO, check_disk=False)

    def test_valid_precise_provenance(self):
        self.assertEqual(self.verify()["origin_commit"], "d" * 40)

    def test_source_version_change_rejected(self):
        data = json.loads(self.meta.read_text())
        data["source_hashes"]["tests/m204-software-workload.sh"] = "0" * 64
        self.meta.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, "provenance mismatch"):
            self.verify()

    def test_corrupt_public_evidence_rejected(self):
        self.pcap.write_bytes(b"tampered capture")
        with self.assertRaisesRegex(ValueError, "hash mismatch"):
            self.verify()

    def test_wrong_architecture_rejected(self):
        data = json.loads(self.meta.read_text())
        data["port"] = "arm64"
        self.meta.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, "provenance mismatch"):
            self.verify()

    def test_missing_clean_second_guest_shutdown_never_cached(self):
        self.write_reopen(missing="[LWEXT4 GUEST] clean upstream unmount and durable flush")
        with self.assertRaisesRegex(ValueError, "INELIGIBLE"):
            module.validate_reopen_uart(self.reopen,
                                        [package for _, package in module.package_manifest(REPO)])
        target = self.base / "unverified.img"
        module.capture("amd64", self.root, self.disk, self.serial, self.pcap, self.reopen,
                       target, self.base / "unverified.json",
                       self.base / "unverified.serial", self.base / "unverified.pcap",
                       self.base / "unverified.reopen", REPO)
        self.assertFalse(target.exists())

    def test_partial_real_package_installs_never_cached(self):
        self.write_serial(bad_install="redis")
        with self.assertRaisesRegex(ValueError, "INELIGIBLE"):
            module.validate_uart(REPO, self.serial)

    def test_preexisting_reopen_marker_cannot_replace_current_replay(self):
        # Valid cached prior evidence is not proof of a fresh third boot;
        # the independent guest --replay ledger and UART are required.
        self.write_reopen(missing="M204_REOPEN_DISCOVERY_COMPLETE")
        with self.assertRaisesRegex(ValueError, "INELIGIBLE"):
            module.validate_reopen_uart(self.reopen,
                                        [package for _, package in module.package_manifest(REPO)])

    def test_symlink_checkpoint_rejected(self):
        path = self.base / "redirect.img"
        path.symlink_to(self.disk)
        with self.assertRaisesRegex(ValueError, "symlink evidence"):
            module.verify("amd64", self.root, path, self.meta,
                          self.serial, self.pcap, self.reopen, REPO, check_disk=False)


if __name__ == "__main__":
    unittest.main()
