#!/usr/bin/env python3
"""Build and verify M249's physical Debian/glibc ext4 guest root.

The source tree is a *minimal*, authenticated Debian 13 package subset,
never a full Debian installation.  The kernel, not a host chroot, executes
its real Debian dash PID1 from this disk.  This host-side command is not
itself a NaSeed guest PASS.
"""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


HERE = Path(__file__).resolve().parent
M191 = HERE / "m191-alpine-root-disk.py"
SPEC = importlib.util.spec_from_file_location("m249_ext4_m191_geometry", M191)
assert SPEC and SPEC.loader
m191 = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(m191)

PORTS = {
    "amd64": (62, "x86_64-linux-gnu", "ld-linux-x86-64.so.2"),
    "riscv64": (243, "riscv64-linux-gnu", "ld-linux-riscv64-lp64d.so.1"),
    "arm64": (183, "aarch64-linux-gnu", "ld-linux-aarch64.so.1"),
}
PROGRAMS = ("dash", "true", "cat", "wc", "printf")
DISK_BYTES = m191.DISK_SIZE
ROOT_PART_BYTES = m191.PART_SECTORS * m191.SECTOR
READBACK = ("etc/m249-init.sh", "etc/m249-input", "usr/bin/dash",
            "usr/bin/true", "usr/bin/cat", "usr/bin/wc", "usr/bin/printf")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError("[M249 DEBIAN EXT4 FAIL] " + message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def validate(port: str, root: Path, manifest_path: Path) -> dict[str, str]:
    require(port in PORTS, "unrecognized ISA")
    require(root.is_dir() and not root.is_symlink(), "missing physical root tree")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    require(manifest.get("architecture") == port and
            manifest.get("distribution") == "Debian 13 trixie" and
            manifest.get("status") == "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS",
            "signed M248 provenance is absent or mismatched")
    # This comparison checks all five source ELF objects byte-for-byte
    # against the authenticated Debian package extraction manifest.
    machine, triplet, loader = PORTS[port]
    files = {f"usr/bin/{name}": name for name in PROGRAMS}
    files[f"usr/lib/{triplet}/libc.so.6"] = "libc.so.6"
    files[f"usr/lib/{triplet}/{loader}"] = "loader"
    signed = manifest.get("elf_sha256", {})
    require(isinstance(signed, dict), "missing signed ELF digests")
    result = {}
    for relative in files:
        path = root / relative
        require(path.is_file() and not path.is_symlink(), f"missing regular {relative}")
        with path.open("rb") as stream:
            header = stream.read(20)
        require(len(header) == 20 and header[:4] == b"\x7fELF" and
                header[4:6] == b"\x02\x01" and
                struct.unpack_from("<H", header, 18)[0] == machine,
                f"invalid native ELF class/ISA: {relative}")
        digest = sha256(path)
        require(signed.get(relative) == digest,
                f"non-Debian ELF bytes or wrong signed package hash: {relative}")
        result[relative] = digest
    expected_lib = "../usr/lib/" + triplet
    link = root / "lib" / triplet
    require(link.is_symlink() and os.readlink(link) == expected_lib,
            "merged-/usr glibc lib path changed")
    interp = (root / "lib64" / loader if port == "amd64" else root / "lib" / loader)
    require(interp.is_symlink() and
            os.readlink(interp) == expected_lib + "/" + loader,
            "original PT_INTERP guest symlink changed")
    init = root / "etc/m249-init.sh"
    require(init.is_file() and init.stat().st_mode & 0o111,
            "missing executable on-disk PID1 shell script")
    require((root / "etc/m249-input").read_bytes() == b"M249-EXT4-INPUT\n",
            "baseline on-disk data changed")
    for directory in ("dev", "etc", "tmp", "var/tmp", "proc", "sys"):
        require((root / directory).is_dir(), "missing guest directory " + directory)
    return result


def extract_partition(disk: Path, destination: Path) -> None:
    m191.extract_partition(disk, destination)


def verify(port: str, root: Path, disk: Path, manifest: Path, after: bool) -> None:
    validate(port, root, manifest)
    m191.check_mbr(disk)
    with tempfile.TemporaryDirectory(prefix="m249-debian-verify-", dir=disk.parent) as work:
        partition = Path(work) / "root.ext4"
        extract_partition(disk, partition)
        m191.check_fsck(partition)
        for relative in READBACK:
            observed = m191.debugfs_cat(partition, "/" + relative)
            expected = (root / relative).read_bytes()
            require(observed == expected, "on-disk signed or baseline bytes changed: " + relative)
        persisted = m191.debugfs_cat(partition, "/var/tmp/m249-persisted")
        if after:
            require(persisted == b"M249-EXT4-GUEST-WROTE\n",
                    "guest persistent ext4 write missing or wrong")
        else:
            require(persisted == b"", "baseline physical ext4 already contains guest result")
    print(f"[M249 DISK {'POST-QEMU' if after else 'PRE-QEMU'} VERIFIED] "
          f"{port} MBR+ext4, signed original Debian/glibc ELF, "
          f"{'persistent guest write' if after else 'clean initial root'}", flush=True)


def prepare(port: str, root: Path, disk: Path, manifest: Path) -> None:
    validate(port, root, manifest)
    require(m191.DISK_MIB == "256", "expected canonical 256MiB root disk")
    disk.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="m249-debian-build-", dir=disk.parent) as work:
        area = Path(work)
        part = area / "debian.ext4"
        with part.open("wb") as output:
            output.truncate(ROOT_PART_BYTES)
        subprocess.run(["mke2fs", "-q", "-F", "-t", "ext4", "-b", "4096",
                        "-O", "^metadata_csum,^64bit", "-d", str(root), str(part)],
                       check=True)
        m191.check_fsck(part)
        staged = area / "debian-root.img"
        with staged.open("wb") as output:
            output.truncate(DISK_BYTES)
        with staged.open("r+b") as output, part.open("rb") as source:
            mbr = bytearray(512)
            mbr[446:462] = (bytes((0, 0, 2, 0, 0x83, 0xFE, 0xFF, 0xFF)) +
                            struct.pack("<II", m191.PART_START, m191.PART_SECTORS))
            mbr[510:512] = b"\x55\xaa"
            output.write(mbr)
            output.seek(m191.PART_START * m191.SECTOR)
            shutil.copyfileobj(source, output, 1024 * 1024)
            output.flush()
            os.fsync(output.fileno())
        verify(port, root, staged, manifest, after=False)
        os.replace(staged, disk)
    print(f"[M249 SIGNED DEBIAN DISK PREPARED, NOT GUEST PASS] {port} {disk}", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("prepare", "before", "after"))
    parser.add_argument("port", choices=PORTS)
    parser.add_argument("root", type=Path)
    parser.add_argument("disk", type=Path)
    parser.add_argument("manifest", type=Path)
    args = parser.parse_args()
    if args.mode == "prepare":
        prepare(args.port, args.root.resolve(), args.disk.resolve(), args.manifest.resolve())
    else:
        verify(args.port, args.root.resolve(), args.disk.resolve(),
               args.manifest.resolve(), after=args.mode == "after")


if __name__ == "__main__":
    main()
