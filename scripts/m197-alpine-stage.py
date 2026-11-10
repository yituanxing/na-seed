#!/usr/bin/env python3
"""Stage only M197 test commands and a live QEMU-DHCP DNS view.

Official Alpine repositories, APK keys, CA, PID1 and executables are never
replaced. The test shell source is not an executable program under test.
"""

import importlib.util
from pathlib import Path
import sys


def load_m191():
    path = Path(__file__).with_name("m191-alpine-root-disk.py")
    spec = importlib.util.spec_from_file_location("m197_m191_disk", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("M197 missing original root-disk path resolver")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def configure(signed_root: Path, overlay: Path) -> None:
    m191 = load_m191()
    if signed_root.is_symlink() or overlay.is_symlink():
        raise ValueError("[M197 STAGE FAIL] symlinked root directories")
    signed_root = signed_root.resolve(strict=True)
    overlay = overlay.resolve(strict=True)
    if signed_root == overlay or not signed_root.is_dir() or not overlay.is_dir():
        raise ValueError("[M197 STAGE FAIL] separate signed root and overlay required")
    official = ("https://dl-cdn.alpinelinux.org/alpine/v3.23/main\n"
                "https://dl-cdn.alpinelinux.org/alpine/v3.23/community\n")
    for root in (signed_root, overlay):
        repo = m191.guest_path(root, "/etc/apk/repositories")
        keys = m191.guest_path(root, "/etc/apk/keys")
        ca = m191.guest_path(root, "/etc/ssl/cert.pem")
        if (repo.read_text() != official or not keys.is_dir() or
                not list(keys.glob("*.pub")) or not ca.is_file() or
                ca.stat().st_size < 1024):
            raise ValueError("[M197 STAGE FAIL] original public trust changed")
    parent = overlay / "etc"
    # The leaf is deliberately replaced, but the host must never follow a
    # staged parent symlink while unlinking/writing it. M191 guest_path is
    # safe for guest reads; host mutation requires a real parent directory.
    if (parent.is_symlink() or not parent.is_dir() or
            parent.resolve(strict=True) != overlay / "etc"):
        raise ValueError("[M197 STAGE FAIL] unsafe resolver parent directory")
    resolv = parent / "resolv.conf"
    if resolv.is_symlink() or resolv.is_file():
        resolv.unlink()
    elif resolv.exists():
        raise ValueError("[M197 STAGE FAIL] unexpected resolver inode")
    resolv.parent.mkdir(parents=True, exist_ok=True)
    resolv.symlink_to("/proc/net/naseed-resolv.conf")
    assert resolv.is_symlink() and resolv.readlink() == Path(
        "/proc/net/naseed-resolv.conf")
    # In contrast to M196's static test resolver, no nameserver value is
    # written to the image. lwIP's real DHCP lease owns the procfs content.
    print("[M197 STAGE PASS] signed Alpine unmodified, DNS from live DHCP procfs")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: m197-alpine-stage.py SIGNED_ROOT OVERLAY")
    configure(Path(sys.argv[1]), Path(sys.argv[2]))
