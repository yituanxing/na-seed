#!/usr/bin/env python3
"""Host-only isolation test: Alpine guest-absolute links must stay guest-local."""

from pathlib import Path
import runpy
import struct
import subprocess
import tempfile


def native_elf(machine: int, marker: int) -> bytes:
    header = bytearray(64)
    header[:6] = b"\x7fELF\x02\x01"
    struct.pack_into("<H", header, 18, machine)
    header[60] = marker
    return bytes(header)


def write(root: Path, name: str, data: bytes) -> None:
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


arch = {
    "amd64": ("x86_64", 62),
    "riscv64": ("riscv64", 243),
    "arm64": ("aarch64", 183),
}
root_model = runpy.run_path("scripts/m191-alpine-root-disk.py")
with tempfile.TemporaryDirectory(prefix="m191-guest-absolute-export-") as temp:
    for port, (alpine_arch, machine) in arch.items():
        base = Path(temp) / port
        root = base / "rootfs"
        root.mkdir(parents=True)
        (root / "bin").symlink_to("/usr/bin")
        (root / "lib").symlink_to("/usr/lib")
        for directory in ("dev/pts", "proc", "sys", "run", "tmp"):
            (root / directory).mkdir(parents=True, exist_ok=True)
        write(root, "etc/alpine-release", b"3.23.7\n")
        write(root, "etc/apk/world", b"alpine-base\nopenrc\n")
        write(root, "etc/apk/repositories", (
            b"https://dl-cdn.alpinelinux.org/alpine/v3.23/main\n"
            b"https://dl-cdn.alpinelinux.org/alpine/v3.23/community\n"))
        write(root, "etc/apk/keys/alpine-devel.pub", b"test-only\n")
        original_inittab = "::sysinit:/sbin/openrc sysinit\n"
        for index in range(1, 7):
            original_inittab += f"tty{index}::respawn:/sbin/getty 38400 tty{index}\n"
        write(root, "etc/inittab", original_inittab.encode())
        root_model["configure_serial_inittab"](root)
        original_busybox = native_elf(machine, 0x37)
        original_loader = native_elf(machine, 0x42)
        write(root, "usr/bin/busybox", original_busybox)
        write(root, f"usr/lib/ld-musl-{alpine_arch}.so.1", original_loader)
        write(root, "sbin/openrc", native_elf(machine, 0x51))
        write(root, "usr/bin/apk", native_elf(machine, 0x52))
        (root / "sbin/init").symlink_to("/bin/busybox")
        output = base / "export"
        subprocess.run([
            "python3", "scripts/m191-export-alpine-elf.py", port,
            str(root), str(output),
        ], check=True)
        if (output / "busybox").read_bytes() != original_busybox:
            raise AssertionError("guest-absolute /bin escaped the Alpine root")
        if (output / f"ld-musl-{alpine_arch}.so.1").read_bytes() != original_loader:
            raise AssertionError("guest-absolute /lib escaped the Alpine root")

print("[M191 ELF EXPORT HOST PASS] three ISA, guest-absolute /bin and /lib remain inside guest root")
