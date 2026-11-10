#!/usr/bin/env python3
"""M254 signed Debian shell-script source and guest-only ext4 output oracle."""

import argparse
import importlib.util
from pathlib import Path
import tempfile


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("m252_disk_for_m254", HERE / "m252-debian-ext4-disk.py")
assert spec and spec.loader
m252 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m252)
m191 = m252.m191

SCRIPT = "etc/m254-guest-workload.sh"
RESULTS = {
    "var/tmp/m254-lifecycle/input": b"alpha\nbeta\ngamma\n",
    "var/tmp/m254-lifecycle/pipeline": b"M254-PIPE-OK\n",
    "var/tmp/m254-lifecycle/result": b"M254-DURABLE\n",
}


def verify(port: str, root: Path, disk: Path, base: Path, m251: Path,
           m252_manifest: Path, after: bool) -> None:
    m252.verify(port, root, disk, base, m251, m252_manifest, after=after)
    source = (HERE / "m254-guest-workload.sh").read_bytes()
    staged = root / SCRIPT
    if not staged.is_file() or staged.is_symlink() or staged.read_bytes() != source:
        raise ValueError("M254 physical root must contain unmodified reviewed guest script")
    if not staged.stat().st_mode & 0o111:
        raise ValueError("M254 guest script must be executable on ext4")
    with tempfile.TemporaryDirectory(prefix="m254-disk-oracle-", dir=disk.parent) as work:
        part = Path(work) / "root.ext4"
        m191.extract_partition(disk, part)
        m191.check_fsck(part)
        if m191.debugfs_cat(part, "/" + SCRIPT) != source:
            raise ValueError("M254 guest script changed on physical disk")
        for relative, expected in RESULTS.items():
            actual = m191.debugfs_cat(part, "/" + relative)
            if actual != (expected if after else b""):
                phase = "post-QEMU" if after else "pre-QEMU"
                raise ValueError(f"M254 {phase} physical ext4 result wrong: {relative}")
    print(f"[M254 SIGNED DEBIAN SCRIPT PHYSICAL EXT4 "
          f"{'POST-NATIVE-QEMU' if after else 'SEED'} VERIFIED] {port}", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("before", "after"))
    parser.add_argument("port", choices=m252.m249.PORTS)
    parser.add_argument("root", type=Path)
    parser.add_argument("disk", type=Path)
    parser.add_argument("base", type=Path)
    parser.add_argument("m251", type=Path)
    parser.add_argument("m252", type=Path)
    args = parser.parse_args()
    verify(args.port, args.root.resolve(), args.disk.resolve(),
           args.base.resolve(), args.m251.resolve(), args.m252.resolve(),
           after=args.mode == "after")


if __name__ == "__main__":
    main()
