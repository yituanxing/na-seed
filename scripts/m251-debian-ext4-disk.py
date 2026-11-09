#!/usr/bin/env python3
"""M251 physical ext4 readback of 7 signed Debian 13 coreutils + glibc DSOs.

Retains M249/M250 source manifest, baseline executables, launcher, and
ext4 preparation behavior. Only native NaSeed QEMU execution can generate
the required M251 persistent marker; seed preparation is never a guest PASS.
"""

import argparse
import importlib.util
from pathlib import Path
import tempfile


HERE = Path(__file__).resolve().parent


def import_script(module: str, filename: str):
    spec = importlib.util.spec_from_file_location(module, HERE / filename)
    assert spec and spec.loader
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


m250 = import_script("m250_signed_debian_disk_for_m251", "m250-debian-ext4-disk.py")
m249 = m250.m249
m191 = m249.m191
provenance = import_script("m251_signed_debian_provenance_for_disk", "m251-debian-provenance.py")

PERSISTED_PATH = "var/tmp/m251-dir/result"
PERSISTED = b"M251-DURABLE\n"
READBACK = ("etc/m251-init.sh", "etc/m251-input")


def require(ok: bool, reason: str) -> None:
    if not ok:
        raise ValueError("[M251 DEBIAN EXT4 FAIL] " + reason)


def verify(port: str, root: Path, disk: Path, manifest: Path,
           extra_manifest: Path, after: bool) -> None:
    root, disk, manifest, extra_manifest = (x.resolve() for x in
                                            (root, disk, manifest, extra_manifest))
    # Preserve M249's baseline and M250's separate test-only CTTY launcher.
    m249.validate(port, root, manifest)
    launcher = m250.verified_launcher(port, root)
    extra = provenance.verify_root(port, root, manifest, extra_manifest)
    for relative in READBACK:
        path = root / relative
        require(path.is_file() and not path.is_symlink(),
                f"missing real on-disk M251 guest input: {relative}")
    require((root / "etc/m251-init.sh").stat().st_mode & 0o111,
            "M251 guest init script not executable")
    m191.check_mbr(disk)
    with tempfile.TemporaryDirectory(prefix="m251-debian-verify-", dir=disk.parent) as work:
        partition = Path(work) / "root.ext4"
        m191.extract_partition(disk, partition)
        m191.check_fsck(partition)
        # Prior signed gates must survive the M251 disk build unchanged.
        expected = (*m249.READBACK, m250.CTTY, "etc/m250-input", *READBACK,
                    *sorted(extra["elf_sha256"]))
        for relative in expected:
            require(m191.debugfs_cat(partition, "/" + relative) ==
                    (root / relative).read_bytes(),
                    f"physical ext4 signed program/DSO/input readback mismatch: {relative}")
        guest_result = m191.debugfs_cat(partition, "/" + PERSISTED_PATH)
        if after:
            require(guest_result == PERSISTED,
                    "NaSeed guest did not persist the exact M251 marker")
        else:
            require(guest_result == b"",
                    "M251 seed already contains the synthetic guest result")
    print(f"[M251 DISK {'POST-NATIVE-QEMU' if after else 'PRE-NATIVE-QEMU'} VERIFIED] "
          f"{port} MBR/ext4 CLEAN, M248-250 unchanged, "
          f"{len(provenance.PROGRAMS)} authenticated Debian 13 coreutils, "
          f"{len(extra['libraries'])} signed dependency DSOs, "
          f"CTTY sha256={launcher}, "
          f"{'durable real guest marker' if after else 'no preseeded guest marker'}",
          flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("prepare", "before", "after"))
    parser.add_argument("port", choices=m249.PORTS)
    parser.add_argument("root", type=Path)
    parser.add_argument("disk", type=Path)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("extra", type=Path)
    args = parser.parse_args()
    root, disk, manifest, extra = (x.resolve() for x in
                                   (args.root, args.disk, args.manifest, args.extra))
    provenance.verify_root(args.port, root, manifest, extra)
    if args.mode == "prepare":
        m250.verified_launcher(args.port, root)
        # Delegates exact canonical 256MiB MBR/ext4 geometry and original
        # signed M249/M250 baseline verification to existing builders.
        m249.prepare(args.port, root, disk, manifest)
    verify(args.port, root, disk, manifest, extra, after=args.mode == "after")


if __name__ == "__main__":
    main()
