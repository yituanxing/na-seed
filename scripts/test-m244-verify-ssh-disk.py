#!/usr/bin/env python3
"""Fast negative contracts for the independent M244 host SSH/PTy receipts."""

import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


source = Path(__file__).with_name("m244-verify-ssh-disk.py")
spec = importlib.util.spec_from_file_location("m244_disk_contract", source)
assert spec and spec.loader
subject = importlib.util.module_from_spec(spec)
spec.loader.exec_module(subject)


def valid_receipt(phase="first"):
    nonce = "a" * 48 if phase == "first" else "b" * 48
    path, value = subject.ACK[phase]
    return {
        "phase": phase,
        "transport": "host-localhost-QEMU-user-hostfwd-guest-eth0-original-dropbear-ssh-pty",
        "host": "127.0.0.1", "host_port": 12222, "guest_port": 15446,
        "session_count": 1, "pty_allocated": True, "public_key_only": True,
        "root_only_isolated_test_no_DAC": True,
        "guest_ssh_ack_path": path,
        "guest_ssh_ack_sha256": hashlib.sha256(value).hexdigest(),
        "client_pubkey_sha256": "f" * 64,
        "host_key_fingerprint": "SHA256:" + "a" * 43,
        "sessions": [{
            "session": 1, "nonce": nonce,
            "nonce_sha256": hashlib.sha256(nonce.encode()).hexdigest(),
            "tty_allocated": True, "canonical_backspace": True,
            "foreground_sigint": True, "winsize_33x101": True,
            "terminal_cursor_queries_answered": 1,
            "remote_stdout_sha256": "e" * 64,
        }],
    }


class ReceiptContract(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="m244-host-proof-unit-")
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "real-host-receipt.json"

    def write(self, document):
        self.path.write_text(json.dumps(document), encoding="utf-8")

    def test_first_valid(self):
        self.write(valid_receipt())
        self.assertEqual(subject.read_receipt(self.path, "first")["host_port"], 12222)

    def test_second_valid(self):
        self.write(valid_receipt("reopen"))
        self.assertEqual(subject.read_receipt(self.path, "reopen")["session_count"], 1)

    def test_missing_proof_rejected(self):
        with self.assertRaises(ValueError):
            subject.read_receipt(self.path, "first")

    def test_symlink_proof_rejected(self):
        target = Path(self.temp.name) / "some-receipt"
        target.write_text("{}", encoding="utf-8")
        self.path.symlink_to(target)
        with self.assertRaises(ValueError):
            subject.read_receipt(self.path, "first")

    def assert_bad(self, field, value):
        doc = valid_receipt()
        doc[field] = value
        self.write(doc)
        with self.assertRaises(ValueError):
            subject.read_receipt(self.path, "first")

    def test_other_boot_rejected(self):
        self.assert_bad("phase", "reopen")

    def test_forged_ack_path_rejected(self):
        self.assert_bad("guest_ssh_ack_path", "/root/old-receipt")

    def test_wrong_ack_bytes_rejected(self):
        self.assert_bad("guest_ssh_ack_sha256", "f" * 64)

    def test_missing_pty_rejected(self):
        self.assert_bad("pty_allocated", False)

    def test_boolean_session_count_rejected(self):
        self.assert_bad("session_count", True)

    def test_unrelated_host_port_rejected(self):
        self.assert_bad("host_port", 22)

    def test_stale_pty_challenge_rejected(self):
        doc = valid_receipt()
        doc["sessions"][0]["nonce_sha256"] = "f" * 64
        self.write(doc)
        with self.assertRaises(ValueError):
            subject.read_receipt(self.path, "first")

    def test_no_canonical_backspace_rejected(self):
        doc = valid_receipt()
        doc["sessions"][0]["canonical_backspace"] = False
        self.write(doc)
        with self.assertRaises(ValueError):
            subject.read_receipt(self.path, "first")


if __name__ == "__main__":
    unittest.main()
