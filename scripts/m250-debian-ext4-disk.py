#!/usr/bin/env python3
"""M250 physical signed Debian ext4 guest boot and interactive write oracle.

Preparation delegates to the already-native-PASS M249 signed Debian disk
builder. The M250-only static CTTY launcher is clearly not a Debian binary.
Post-QEMU only, verify a CLEAN ext4 and physically persisted UART shell data.
"""

import argparse
import hashlib
import importlib.util
from pathlib import Path
import tempfile

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("m249_signed_debian_disk", HERE / "m249-debian-ext4-disk.py")
assert SPEC and SPEC.loader
m249 = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(m249)
m191 = m249.m191

INPUT = b"M250-EXT4-INPUT\n"
PERSISTED = b"M250-PERSISTED\n"
CTTY = "sbin/m250-tty-launch"


def require(ok: bool, reason: str) -> None:
    if not ok:
        raise ValueError("[M250 DEBIAN EXT4 FAIL] " + reason)


def verified_launcher(port: str, root: Path) -> str:
    machine, _, _ = m249.PORTS[port]
    path = root / CTTY
    require(path.is_file() and not path.is_symlink(), "CTTY helper must be a real ext4 ELF")
    with path.open("rb") as stream:
        hdr = stream.read(20)
    require(len(hdr) == 20 and hdr[:6] == b"\x7fELF\x02\x01" and
            int.from_bytes(hdr[18:20], "little") == machine,
            "test-only CTTY helper has wrong native ELF class or ISA")
    require(path.stat().st_mode & 0o111, "CTTY ELF must be executable")
    require((root / "etc/m250-input").read_bytes() == INPUT,
            "guest baseline input changed")
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(port: str, root: Path, disk: Path, manifest: Path, after: bool) -> None:
    # Read and hash ALL seven authenticated original Debian ELF objects,
    # including the glibc PT_INTERP loader, and reject local substitutions.
    m249.validate(port, root, manifest)
    digest = verified_launcher(port, root)
    m191.check_mbr(disk)
    with tempfile.TemporaryDirectory(prefix="m250-debian-verify-", dir=disk.parent) as work:
        partition = Path(work) / "root.ext4"
        m191.extract_partition(disk, partition)
        # This e2fsck step must actually run after QEMU, never on a seed-only
        # image. An unclean shutdown cannot be presented as a guest PASS.
        m191.check_fsck(partition)
        for relative in (*m249.READBACK, CTTY, "etc/m250-input"):
            require(m191.debugfs_cat(partition, "/" + relative) ==
                    (root / relative).read_bytes(),
                    "physical ext4 original/dependency/helper readback mismatch: " + relative)
        persisted = m191.debugfs_cat(partition, "/var/tmp/m250-persisted")
        if after:
            require(persisted == PERSISTED,
                    "UART-driven original Debian dash failed durable ext4 write")
        else:
            require(persisted == b"", "baseline already contains UART result")
    print(f"[M250 DISK {'POST-QEMU' if after else 'PRE-QEMU'} VERIFIED] "
          f"{port} signed Debian/glibc intact, MBR/ext4 clean, "
          f"CTTY sha256={digest}, "
          f"{'interactive shell write persisted' if after else 'no synthetic guest result'}",
          flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("prepare", "before", "after"))
    parser.add_argument("port", choices=m249.PORTS)
    parser.add_argument("root", type=Path)
    parser.add_argument("disk", type=Path)
    parser.add_argument("manifest", type=Path)
    args = parser.parse_args()
    root, disk, manifest = args.root.resolve(), args.disk.resolve(), args.manifest.resolve()
    m249.validate(args.port, root, manifest)
    verified_launcher(args.port, root)
    if args.mode == "prepare":
        m249.prepare(args.port, root, disk, manifest)
    verify(args.port, root, disk, manifest, after=args.mode == "after")


if __name__ == "__main__":
    main()
