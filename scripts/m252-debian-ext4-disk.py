#!/usr/bin/env python3
"""M252 physical MBR/ext4 proof for original Debian grep, sed and find.

Host verification confirms signed, unmodified Debian ELF and inputs; only
native NaSeed QEMU can create M252-DURABLE on the physical ext4 partition.
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


m251disk = import_script("m251_signed_debian_disk_for_m252",
                         "m251-debian-ext4-disk.py")
m249, m250, m191 = m251disk.m249, m251disk.m250, m251disk.m191
provenance = import_script("m252_signed_debian_provenance_for_disk",
                           "m252-debian-provenance.py")

PERSISTED_PATH = "var/tmp/m252-dir/result"
PERSISTED = b"M252-DURABLE\n"
READBACK = ("etc/m252-init.sh", "etc/m252-input")


def require(condition: bool, reason: str) -> None:
    if not condition:
        raise ValueError("[M252 SIGNED EXT4 FAIL] " + reason)


def verify(port: str, root: Path, disk: Path, base_manifest: Path,
           m251_manifest: Path, m252_manifest: Path, after: bool) -> None:
    root, disk, base_manifest, m251_manifest, m252_manifest = (
        path.resolve() for path in
        (root, disk, base_manifest, m251_manifest, m252_manifest))
    # Important: the M252 init, not the M251 test, runs on this disk.
    # M251's guest marker must remain absent even after successful M252.
    m251disk.verify(port, root, disk, base_manifest, m251_manifest,
                    after=False)
    extra = provenance.verify_root(port, root, base_manifest,
                                   m251_manifest, m252_manifest)
    for relative in READBACK:
        path = root / relative
        require(path.is_file() and not path.is_symlink(),
                f"missing original guest M252 script/input: {relative}")
    require((root / "etc/m252-init.sh").stat().st_mode & 0o111,
            "M252 guest init script not executable")
    m191.check_mbr(disk)
    with tempfile.TemporaryDirectory(prefix="m252-signed-disk-", dir=disk.parent) as work:
        partition = Path(work) / "root.ext4"
        m191.extract_partition(disk, partition)
        m191.check_fsck(partition)
        for relative in (*READBACK, *sorted(extra["elf_sha256"])):
            require(m191.debugfs_cat(partition, "/" + relative) ==
                    (root / relative).read_bytes(),
                    f"physical signed ELF/guest input changed: {relative}")
        marker = m191.debugfs_cat(partition, "/" + PERSISTED_PATH)
        if after:
            require(marker == PERSISTED,
                    "NaSeed guest did not persist exact M252 durable result")
        else:
            require(marker == b"",
                    "M252 disk was preseeded with synthetic guest result")
    print(f"[M252 SIGNED EXT4 {'POST-NATIVE-QEMU' if after else 'HOST SEED'} "
          f"VERIFIED] {port}: 3 original Debian 13/glibc ELF; "
          f"{len(extra['libraries'])} new signed DSOs, "
          f"{len(extra['reused_libraries'])} inherited signed DSOs, "
          f"M248-M251 and MBR/ext4 clean; "
          f"{'native durable marker read back' if after else 'no guest marker preseeded'}",
          flush=True)


def prepare(port: str, root: Path, disk: Path, base_manifest: Path,
            m251_manifest: Path, m252_manifest: Path) -> None:
    root, disk, base_manifest, m251_manifest, m252_manifest = (
        path.resolve() for path in
        (root, disk, base_manifest, m251_manifest, m252_manifest))
    provenance.verify_root(port, root, base_manifest,
                           m251_manifest, m252_manifest)
    m250.verified_launcher(port, root)
    # Reuse authenticated M249 physical 256 MiB MBR/ext4 seed builder.
    m249.prepare(port, root, disk, base_manifest)
    verify(port, root, disk, base_manifest, m251_manifest, m252_manifest,
           after=False)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("prepare", "before", "after"))
    parser.add_argument("port", choices=m249.PORTS)
    parser.add_argument("root", type=Path)
    parser.add_argument("disk", type=Path)
    parser.add_argument("base_manifest", type=Path)
    parser.add_argument("m251_manifest", type=Path)
    parser.add_argument("m252_manifest", type=Path)
    args = parser.parse_args()
    if args.mode == "prepare":
        prepare(args.port, args.root, args.disk, args.base_manifest,
                args.m251_manifest, args.m252_manifest)
    else:
        verify(args.port, args.root, args.disk, args.base_manifest,
               args.m251_manifest, args.m252_manifest, after=args.mode == "after")


if __name__ == "__main__":
    main()
