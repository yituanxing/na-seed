#!/usr/bin/env python3
"""Dependency-free regressions for M191 rootfs metadata and MBR guards."""

from pathlib import Path
import runpy
import struct
import tempfile


model = runpy.run_path("scripts/m191-alpine-root-disk.py")


def write(root: Path, name: str, data: bytes) -> None:
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def native_elf(machine: int) -> bytes:
    header = bytearray(64)
    header[:6] = b"\x7fELF\x02\x01"
    struct.pack_into("<H", header, 18, machine)
    return bytes(header)


def expect_fail(action, reason: str) -> None:
    try:
        action()
    except ValueError as error:
        if reason not in str(error):
            raise AssertionError(f"wrong rejection: {error}") from error
    else:
        raise AssertionError("accepted invalid M191 filesystem/disk: " + reason)


with tempfile.TemporaryDirectory(prefix="m191-host-smoke-") as temp:
    base = Path(temp)
    for port, machine in model["ARCH_MACHINE"].items():
        root = base / port
        write(root, "etc/alpine-release", b"3.23.7\n")
        write(root, "etc/apk/world", b"alpine-base\nopenrc\n")
        write(root, "etc/apk/repositories", (
            b"https://dl-cdn.alpinelinux.org/alpine/v3.23/main\n"
            b"https://dl-cdn.alpinelinux.org/alpine/v3.23/community\n"))
        write(root, "etc/apk/keys/alpine-devel.pub", b"official-key-placeholder\n")
        inittab = "::sysinit:/sbin/openrc sysinit\n"
        for index in range(1, 7):
            inittab += f"tty{index}::respawn:/sbin/getty 38400 tty{index}\n"
        write(root, "etc/inittab", inittab.encode())
        write(root, "bin/busybox", native_elf(machine))
        write(root, "sbin/openrc", native_elf(machine))
        write(root, "usr/bin/apk", native_elf(machine))
        (root / "sbin/init").symlink_to("/bin/busybox")
        for directory in ("dev/pts", "proc", "sys", "run", "tmp"):
            (root / directory).mkdir(parents=True, exist_ok=True)
        (root / "var").mkdir()
        (root / "var/run").symlink_to("/run")
        if model["guest_path"](root, "/var/run/state") != root / "run/state":
            raise AssertionError("guest-absolute intermediate link escaped root")
        model["configure_serial_inittab"](root)
        original_serialized = (root / "etc/inittab").read_bytes()
        model["configure_serial_inittab"](root)
        if (root / "etc/inittab").read_bytes() != original_serialized:
            raise AssertionError("Alpine serial getty overlay was not idempotent")
        model["validate_tree"](port, root)
        write(root, "etc/inittab", original_serialized.replace(
            model["SERIAL_GETTY"].encode(),
            b"ttyS0::respawn:/sbin/getty -L ttyS0 9600 vt100"))
        expect_fail(lambda: model["configure_serial_inittab"](root),
                    "conflicting Alpine serial getty")
        write(root, "etc/inittab", original_serialized)
        write(root, "usr/bin/apk", native_elf(999))
        expect_fail(lambda: model["validate_tree"](port, root), "wrong-ISA ELF")
        write(root, "usr/bin/apk", native_elf(machine))
        (root / "sbin/init").unlink()
        (root / "sbin/init").symlink_to("../../../etc/passwd")
        expect_fail(lambda: model["validate_tree"](port, root),
                    "guest symlink escapes root")
    disk = base / "sparse-root.img"
    with disk.open("wb") as output:
        output.truncate(model["DISK_SIZE"])
    expect_fail(lambda: model["check_mbr"](disk), "invalid MBR")
    with disk.open("r+b") as output:
        mbr = bytearray(512)
        start, length = model["PART_START"], model["PART_SECTORS"]
        mbr[446:462] = (bytes((0, 0, 2, 0, 0x83, 0xfe, 0xff, 0xff)) +
                        struct.pack("<II", start, length))
        mbr[510:512] = b"\x55\xaa"
        output.write(mbr)
    model["check_mbr"](disk)

print("[M191 HOST SMOKE PASS] three ISA, native ELF, symlink containment, "
      "signed-package metadata, sparse physical MBR geometry")
