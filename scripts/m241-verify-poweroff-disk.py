#!/usr/bin/env python3
"""Read *post-QEMU* bytes from M241's real physical ext4 partition.

This is independent of the guest's `cat`/`sync` output.  It fails if the
nonce was only cached in RAM, if the wrong disk booted, or if ext4 was left
unclean.  M191 owns the MBR layout and read-only ext4 extraction helpers.
"""

import importlib.util
from pathlib import Path
import sys
import tempfile


EXPECTED = b"M241_REAL_EXT4_POWEROFF_20261109\n"
GUEST_FILE = "/root/m241-poweroff-nonce.txt"


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: m241-verify-poweroff-disk.py DISPOSABLE_PHYSICAL_IMG")
    path = Path(__file__).with_name("m191-alpine-root-disk.py")
    spec = importlib.util.spec_from_file_location("m241_m191_disk", path)
    assert spec and spec.loader
    m191 = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m191)
    disk = Path(sys.argv[1]).resolve(strict=True)
    m191.check_mbr(disk)
    with tempfile.TemporaryDirectory(prefix="m241-poweroff-verify-") as temporary:
        partition = Path(temporary) / "root.ext4"
        m191.extract_partition(disk, partition)
        m191.check_fsck(partition)
        actual = m191.debugfs_cat(partition, GUEST_FILE)
    if actual != EXPECTED:
        raise RuntimeError(
            "[M241 POWER OFF DISK FAIL] original Alpine guest nonce missing "
            "or not persisted in the physical ext4 partition"
        )
    print("[M241 POWER OFF DISK PASS] real guest-written nonce persisted "
          "in clean physical ext4 after unforced QEMU exit")


if __name__ == "__main__":
    main()
