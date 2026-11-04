#!/usr/bin/env python3
"""Independent host proof of Alpine's guest-installed package and HTTP data.

M195 intentionally mutates APK's world/database on a disposable ext4 disk.
The unmodified M191 signed baseline is verified separately, so applying
M191's byte-for-byte world oracle to this intentionally changed disk would
falsely reject legitimate package installation.
"""

import importlib.util
from pathlib import Path
import struct
import sys
import tempfile


WITNESS = b"M195_SIGNED_ALPINE_WIRE_2026\n"


def load_m191():
    source = Path(__file__).with_name("m191-alpine-root-disk.py")
    spec = importlib.util.spec_from_file_location("naseed_m191_disk", source)
    if spec is None or spec.loader is None:
        raise RuntimeError("M191 physical disk oracle unavailable")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def verify(port: str, signed_root: Path, disk: Path) -> None:
    m191 = load_m191()
    if port not in m191.ARCH_MACHINE:
        raise ValueError("[M195 HOST FAIL] unsupported ISA")
    if signed_root.is_symlink() or disk.is_symlink():
        raise ValueError("[M195 HOST FAIL] host source or disk is a symlink")
    signed_root = signed_root.resolve(strict=True)
    disk = disk.resolve(strict=True)
    if not signed_root.is_dir() or not disk.is_file():
        raise ValueError("[M195 HOST FAIL] source or physical disk is missing")
    # A BusyBox applet pathname is not evidence that the separate upstream
    # tree APK was installed. The guest first requires `apk info -e tree` to
    # report absent, then installs it over the captured wire; this check only
    # forbids staging the HTTP witness before the VM begins.
    witness_source = m191.guest_path(signed_root, "/root/m195-wire.txt")
    if witness_source.exists() or witness_source.is_symlink():
        raise ValueError("[M195 HOST FAIL] HTTP witness was staged")

    m191.check_mbr(disk)
    with tempfile.TemporaryDirectory(prefix="m195-alpine-ext4-") as workspace:
        partition = Path(workspace) / "root.ext4"
        m191.extract_partition(disk, partition)
        m191.check_fsck(partition)
        read = lambda path: m191.debugfs_cat(partition, path)

        # The package installer must preserve original signed base state,
        # apart from APK's intentionally changed world and installed index.
        for path in ("/etc/alpine-release", "/etc/apk/repositories", "/etc/inittab"):
            if read(path) != m191.guest_path(signed_root, path).read_bytes():
                raise ValueError("[M195 HOST FAIL] baseline changed: " + path)
        world = read("/etc/apk/world").decode("utf-8")
        if not {"alpine-base", "openrc", "tree"}.issubset(set(world.splitlines())):
            raise ValueError("[M195 HOST FAIL] APK world missing installed package")
        installed = read("/lib/apk/db/installed").decode("utf-8")
        if "P:tree\n" not in "\n" + installed:
            raise ValueError("[M195 HOST FAIL] APK installed database missing tree")
        binary = read("/usr/bin/tree")
        if len(binary) < 20 or binary[:4] != b"\x7fELF" or binary[4:6] != b"\x02\x01":
            raise ValueError("[M195 HOST FAIL] new package's original ELF is absent")
        if struct.unpack_from("<H", binary, 18)[0] != m191.ARCH_MACHINE[port]:
            raise ValueError("[M195 HOST FAIL] new ELF has wrong ISA")
        if read("/root/m195-wire.txt") != WITNESS:
            raise ValueError("[M195 HOST FAIL] exact HTTP file not durable after reboot")

    print(f"[M195 ALPINE PACKAGE PERSISTENCE HOST PASS] {port} guest APK "
          "world+installed DB, native tree ELF, original base metadata, "
          "HTTP witness, second-QEMU reboot and clean ext4 fsck")


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit("usage: m195-alpine-disk-check.py PORT SIGNED_ROOT DISK")
    verify(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]))


if __name__ == "__main__":
    main()
