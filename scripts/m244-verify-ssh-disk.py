#!/usr/bin/env python3
"""Independent M244 same-QEMU upstream Alpine SSH/PTy + physical ext4 gate.

Real host SSH PTY receipts, encrypted guest NIC frames, M243 package+crond
proof and persisted upstream Dropbear identity must ALL agree. The original
signed Alpine root is immutable, and is never a replacement for guest proof.
"""

import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile


ACK = {
    "first": ("/root/m244-first-host-pty.ok", b"M244_HOST_PTY_FIRST_OK_20261109\n"),
    "reopen": ("/root/m244-second-host-pty.ok", b"M244_HOST_PTY_SECOND_OK_20261109\n"),
}
KEY_POLICY = "no-port-forwarding,no-agent-forwarding,no-X11-forwarding"
FINGERPRINT = re.compile(r"SHA256:[A-Za-z0-9+/]{43}\Z")
HEX_64 = re.compile(r"[0-9a-f]{64}\Z")


def sibling(filename: str, name: str):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    if spec is None or spec.loader is None:
        raise RuntimeError("M244 missing signed Alpine native source: " + filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read_receipt(path: Path, phase: str) -> dict:
    if path.is_symlink() or not path.is_file():
        raise ValueError("[M244 HOST FAIL] missing real SSH PTY receipt: " + phase)
    record = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(record, dict) or record.get("phase") != phase:
        raise ValueError("[M244 HOST FAIL] foreign SSH PTY phase: " + phase)
    for field, expected in {
        "host": "127.0.0.1", "host_port": 12222, "guest_port": 15446,
        "session_count": 1, "pty_allocated": True, "public_key_only": True,
        "root_only_isolated_test_no_DAC": True,
        "guest_ssh_ack_path": ACK[phase][0],
        "guest_ssh_ack_sha256": hashlib.sha256(ACK[phase][1]).hexdigest(),
    }.items():
        if type(record.get(field)) is not type(expected) or record[field] != expected:
            raise ValueError("[M244 HOST FAIL] SSH phase receipt invalid " + field)
    session = record.get("sessions")
    if not isinstance(session, list) or len(session) != 1 or not isinstance(session[0], dict):
        raise ValueError("[M244 HOST FAIL] no genuine interactive PTY session")
    item = session[0]
    for field in ("tty_allocated", "canonical_backspace", "foreground_sigint",
                  "winsize_33x101"):
        if item.get(field) is not True:
            raise ValueError("[M244 HOST FAIL] PTY feature missing " + field)
    for field in ("client_pubkey_sha256", "host_key_fingerprint"):
        value = record.get(field)
        pattern = HEX_64 if field == "client_pubkey_sha256" else FINGERPRINT
        if not isinstance(value, str) or pattern.fullmatch(value) is None:
            raise ValueError("[M244 HOST FAIL] SSH Ed25519 proof invalid " + field)
    nonce = item.get("nonce")
    if (not isinstance(nonce, str) or re.fullmatch("[0-9a-f]{48}", nonce) is None
            or item.get("nonce_sha256") != hashlib.sha256(nonce.encode()).hexdigest()):
        raise ValueError("[M244 HOST FAIL] unbound host PTY challenge")
    return record


def verify(port: str, signed_root: Path, disk: Path, first: Path,
           second: Path, pcap: Path) -> None:
    m243 = sibling("m243-verify-cli-disk.py", "m244_m243_native_ext4")
    m191 = sibling("m191-alpine-root-disk.py", "m244_m191_native_disk")
    m230 = sibling("m230-dropbear-disk-check.py", "m244_m230_physical_ssh")
    m240 = sibling("m240-host-ssh-pty-check.py", "m244_m240_host_pty")
    if port not in m191.ARCH_MACHINE:
        raise ValueError("[M244 HOST FAIL] unsupported native ISA")
    inputs = (signed_root, disk, first, second, pcap)
    if any(item.is_symlink() for item in inputs) or len({
            str(item.resolve(strict=False)) for item in inputs}) != len(inputs):
        raise ValueError("[M244 HOST FAIL] symlinked or overlapping proof")
    root = signed_root.resolve(strict=True)
    physical = disk.resolve(strict=True)
    for path in ("/root/.ssh/authorized_keys", "/root/m244-hostkey.sha256",
                 "/etc/dropbear/dropbear_ed25519_host_key",
                 "/etc/runlevels/default/dropbear", *[v[0] for v in ACK.values()]):
        pristine = m191.guest_path(root, path)
        if pristine.exists() or pristine.is_symlink():
            raise ValueError("[M244 HOST FAIL] official Alpine source preseeded " + path)

    a = read_receipt(first, "first")
    b = read_receipt(second, "reopen")
    if (a["client_pubkey_sha256"] != b["client_pubkey_sha256"] or
            a["host_key_fingerprint"] != b["host_key_fingerprint"] or
            a["sessions"][0]["nonce"] == b["sessions"][0]["nonce"] or
            a.get("transport") != m240.TRANSPORT or
            b.get("transport") != m240.TRANSPORT):
        # The host's separately pinned known_hosts is retained across the
        # original platform reset. Two distinct nonce-bound sessions required.
        raise ValueError("[M244 HOST FAIL] SSH identities/challenges changed across reboot")
    # M244 inherits the *already accepted* whole upstream M243 CLI gate:
    # original HTTPS apk tree+jq, OpenRC crond, true same-QEMU reset, ext4 fsck.
    m243.verify(port, root, physical)
    if pcap.is_symlink() or not pcap.is_file() or pcap.stat().st_size < 1024:
        raise ValueError("[M244 WIRE FAIL] missing real QEMU VirtIO SSH capture")
    flow = m230.physical_ssh_flow(pcap.read_bytes(), "same-QEMU-two-epoch")
    if flow["matching_guest_ssh_tcp_sessions"] < 2:
        raise ValueError("[M244 WIRE FAIL] fewer than two real original SSH TCP sessions")

    with tempfile.TemporaryDirectory(prefix="m244-original-alpine-ext4-") as temp:
        partition = Path(temp) / "root.ext4"
        m191.extract_partition(physical, partition)
        read = lambda path: m191.debugfs_cat(partition, path)

        def stat(path):
            return subprocess.run(["debugfs", "-R", "stat " + path, str(partition)],
                                  check=True, text=True, capture_output=True).stdout

        world = read("/etc/apk/world").decode("utf-8").splitlines()
        installed = b"\n" + read("/lib/apk/db/installed")
        for package in ("dropbear", "dropbear-openrc"):
            if package not in world or b"\nP:" + package.encode() + b"\n" not in installed:
                raise ValueError("[M244 HOST FAIL] upstream signed APK not installed: " + package)
        for executable in ("/usr/sbin/dropbear", "/usr/bin/dropbearkey"):
            binary = read(executable)
            if (len(binary) < 20 or binary[:6] != b"\x7fELF\x02\x01" or
                    struct.unpack_from("<H", binary, 18)[0] != m191.ARCH_MACHINE[port]):
                raise ValueError("[M244 HOST FAIL] original Dropbear native ELF changed: " + executable)
        if not read("/etc/init.d/dropbear").startswith(b"#!"):
            raise ValueError("[M244 HOST FAIL] original signed OpenRC dropbear script absent")
        if read("/etc/network/interfaces") != (
            b"auto eth0\niface eth0\n"
            b"    pre-up /bin/sh /root/m244-network-ready.sh\n"
        ) or "Type: symlink" not in stat("/etc/runlevels/boot/networking"):
            raise ValueError("[M244 HOST FAIL] original ifupdown-ng networking configuration not persistent")
        if "Type: symlink" not in stat("/etc/runlevels/default/dropbear"):
            raise ValueError("[M244 HOST FAIL] original OpenRC default dropbear not persisted")
        authorized = read("/root/.ssh/authorized_keys").decode("ascii").strip().splitlines()
        if len(authorized) != 1:
            raise ValueError("[M244 HOST FAIL] original guest authorized_keys count changed")
        parts = authorized[0].split()
        if len(parts) != 3 or parts[0] != KEY_POLICY or parts[1] != "ssh-ed25519":
            raise ValueError("[M244 HOST FAIL] Dropbear authorized-key PTY policy changed")
        client_digest = hashlib.sha256((" ".join(parts[1:]) + "\n").encode()).hexdigest()
        if client_digest != a["client_pubkey_sha256"]:
            raise ValueError("[M244 HOST FAIL] guest SSH client identity does not match real host")
        key = read("/etc/dropbear/dropbear_ed25519_host_key")
        key_stat = stat("/etc/dropbear/dropbear_ed25519_host_key")
        if (len(key) < 80 or "Type: regular" not in key_stat or
                re.search(r"Mode:\s+0*600\b", key_stat) is None):
            raise ValueError("[M244 HOST FAIL] original guest server host key absent")
        if read("/root/m244-hostkey.sha256").strip() != hashlib.sha256(key).hexdigest().encode():
            raise ValueError("[M244 HOST FAIL] guest original Ed25519 server identity changed")
        for path, value in ACK.values():
            if read(path) != value:
                raise ValueError("[M244 HOST FAIL] real authenticated host PTY phase ACK absent")

    print(f"[M244 ORIGINAL ALPINE SSH HOST+WIRE+DISK PASS] {port} genuine two-epoch "
          "host OpenSSH interactive PTYs, original signed Dropbear+OpenRC, "
          "pinned original host identity, physical two-epoch encrypted SSH "
          "traffic, same-QEMU reboot and clean ext4; ROOT-ONLY ISOLATED TEST")


if __name__ == "__main__":
    if len(sys.argv) != 7:
        raise SystemExit("usage: m244-verify-ssh-disk.py ISA SIGNED_ROOT IMG FIRST.json SECOND.json QEMU.pcap")
    verify(sys.argv[1], *map(Path, sys.argv[2:]))
