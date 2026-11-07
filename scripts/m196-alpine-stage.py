#!/usr/bin/env python3
"""Scoped QEMU slirp DNS configuration for a disposable official Alpine root.

Never mutate the M191 signed baseline or replace APK keys, repositories,
CA certificates, PID1, musl, or original programs. Guest absolute symlinks
are resolved with the established M191 guest-root resolver.
"""

import importlib.util
from pathlib import Path
import sys


def load_m191():
    path = Path(__file__).with_name("m191-alpine-root-disk.py")
    spec = importlib.util.spec_from_file_location("naseed_m191", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("M191 rootfs path resolver unavailable")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def configure(signed_root: Path, overlay: Path):
    m191 = load_m191()
    if signed_root.is_symlink() or overlay.is_symlink():
        raise ValueError("[M196 STAGE FAIL] symlinked host roots")
    signed_root = signed_root.resolve(strict=True)
    overlay = overlay.resolve(strict=True)
    if signed_root == overlay or not signed_root.is_dir() or not overlay.is_dir():
        raise ValueError("[M196 STAGE FAIL] independent root copy required")
    official = ("https://dl-cdn.alpinelinux.org/alpine/v3.23/main\n"
                "https://dl-cdn.alpinelinux.org/alpine/v3.23/community\n")
    for root in (signed_root, overlay):
        actual = m191.guest_path(root, "/etc/apk/repositories").read_text()
        if actual != official:
            raise ValueError("[M196 STAGE FAIL] official HTTPS repos replaced")
        keys = m191.guest_path(root, "/etc/apk/keys")
        if not keys.is_dir() or not list(keys.glob("*.pub")):
            raise ValueError("[M196 STAGE FAIL] signed Alpine APK keys missing")
        trust = m191.guest_path(root, "/etc/ssl/cert.pem")
        if not trust.is_file() or trust.stat().st_size < 1024:
            raise ValueError("[M196 STAGE FAIL] official Alpine CA bundle missing")
    # No public address, DNS response or APK is staged: QEMU's documented
    # user-mode DNS forwarder must answer real guest libc lookups at runtime.
    resolv = m191.guest_path(overlay, "/etc/resolv.conf")
    resolv.parent.mkdir(parents=True, exist_ok=True)
    resolv.write_text("nameserver 10.0.2.3\noptions timeout:3 attempts:2\n")
    resolv.chmod(0o644)
    # The baseline may contain an empty, generated or packaging-provided
    # resolv.conf; never rewrite that file on the signed source tree.
    print("[M196 PUBLIC STAGE PASS] signed original Alpine APK/CA unchanged, "
          "disposable QEMU DNS only")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: m196-alpine-stage.py SIGNED_ROOT OVERLAY")
    configure(Path(sys.argv[1]), Path(sys.argv[2]))
