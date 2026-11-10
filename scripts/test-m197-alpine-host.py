#!/usr/bin/env python3
"""Fast host-only fail-closed checks for M197 signed root DNS/workload model."""

import importlib.util
from pathlib import Path
import shutil
import tempfile


ROOT = Path(__file__).resolve().parent.parent


def load(name: str, alias: str):
    target = ROOT / "scripts" / name
    spec = importlib.util.spec_from_file_location(alias, target)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


stage = load("m197-alpine-stage.py", "m197_host_stage")
uart = load("m197-alpine-uart.py", "m197_host_uart")
disk = load("m197-alpine-disk-check.py", "m197_host_disk")

assert uart.PACKAGES == disk.PACKAGES == ("bash", "lua5.4", "sqlite", "git")
assert len(uart.COMMANDS) == 9
assert all(expected and purpose for _, expected, purpose in uart.COMMANDS)
assert "n=$((n+1))" in uart.COMMANDS[0][0]
assert '"$n" -ge 90' in uart.COMMANDS[0][0]
assert "/proc/net/naseed-resolv.conf" in uart.COMMANDS[0][0]
assert uart.COMMANDS[0][1] == "M197_DHCP_DNS_READY_RC=0"
assert "M197_APP_WORKLOAD_PASS" in (ROOT / "tests/m197-software-workload.sh").read_text()
assert "M197_REOPEN_WORKLOAD_PASS" in (ROOT / "tests/m197-software-workload.sh").read_text()

with tempfile.TemporaryDirectory(prefix="m197-stage-contract-") as workspace:
    work = Path(workspace)
    signed = work / "signed"
    overlay = work / "overlay"
    (signed / "etc/apk/keys").mkdir(parents=True)
    (signed / "etc/ssl").mkdir(parents=True)
    (signed / "etc/apk/repositories").write_text(
        "https://dl-cdn.alpinelinux.org/alpine/v3.23/main\n"
        "https://dl-cdn.alpinelinux.org/alpine/v3.23/community\n")
    (signed / "etc/apk/keys/original.pub").write_text("signed-key-fixture")
    (signed / "etc/ssl/cert.pem").write_bytes(b"C" * 2048)
    (signed / "etc/resolv.conf").write_text("original-bundle-owned\n")
    shutil.copytree(signed, overlay)
    stage.configure(signed, overlay)
    resolver = overlay / "etc/resolv.conf"
    assert resolver.is_symlink() and resolver.readlink() == Path(
        "/proc/net/naseed-resolv.conf")
    assert (signed / "etc/resolv.conf").read_text() == "original-bundle-owned\n"
    assert (signed / "etc/apk/repositories").read_bytes() == (
        overlay / "etc/apk/repositories").read_bytes()

    # A weaker, host-supplied or non-TLS repository must fail staging even
    # though the source directory and a QEMU resolver inode exist.
    (overlay / "etc/apk/repositories").write_text("http://10.0.2.2:15417\n")
    try:
        stage.configure(signed, overlay)
    except ValueError as error:
        assert "public trust changed" in str(error)
    else:
        raise AssertionError("host HTTP substitution accepted")

    # A valid-looking sibling directory must not be writable through an
    # overlay /etc parent symlink, even if M191 guest_path can resolve its
    # contents as an in-guest symlink without escaping the signed tree.
    (overlay / "etc/apk/repositories").write_bytes(
        (signed / "etc/apk/repositories").read_bytes())
    (overlay / "etc").rename(overlay / "etc-backed")
    (overlay / "etc").symlink_to("etc-backed", target_is_directory=True)
    try:
        stage.configure(signed, overlay)
    except ValueError as error:
        assert "unsafe resolver parent" in str(error)
    else:
        raise AssertionError("symlinked resolver parent accepted")
    assert (overlay / "etc-backed/resolv.conf").is_symlink()

probe = (ROOT / "scripts/probe-m197-alpine-software-cohort.sh").read_text()
assert "m196-public-wire-check.py" in probe
assert "m197-alpine-disk-check.py" in probe
assert "m191-alpine-root-disk.py verify" in probe
assert "NAS_QEMU_SERIAL_FIFO" in probe and "NAS_VIRTIO_NET_PCAP" in probe
assert "--allow-untrusted" not in probe and "--no-check-certificate" not in probe
assert "sudo install -m 0644 tests/m197-software-workload.sh" in probe
print("[M197 HOST CONTRACT PASS] live proc DHCP DNS, unchanged official "
      "Alpine CA+APK keys, four-package functional cohort, public wire and "
      "second-QEMU durable disk proof")
