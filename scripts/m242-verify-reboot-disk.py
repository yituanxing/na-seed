#!/usr/bin/env python3
"""Independent post-QEMU readback of original Alpine's persisted reboot nonce."""

import importlib.util
from pathlib import Path
import sys
import tempfile


EXPECTED = b"M242_REAL_EXT4_REBOOT_SAME_QEMU_20261109\n"
GUEST_FILE = "/root/m242-reboot-nonce.txt"


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: m242-verify-reboot-disk.py DISPOSABLE_PHYSICAL_IMG")
    path = Path(__file__).with_name("m191-alpine-root-disk.py")
    spec = importlib.util.spec_from_file_location("m242_m191_disk", path)
    assert spec and spec.loader
    m191 = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m191)
    disk = Path(sys.argv[1]).resolve(strict=True)
    m191.check_mbr(disk)
    with tempfile.TemporaryDirectory(prefix="m242-reboot-verify-") as temporary:
        partition = Path(temporary) / "root.ext4"
        m191.extract_partition(disk, partition)
        m191.check_fsck(partition)
        actual = m191.debugfs_cat(partition, GUEST_FILE)
    if actual != EXPECTED:
        raise RuntimeError("[M242 REBOOT DISK FAIL] original Alpine nonce not "
                           "persisted across physical platform reset and second shutdown")
    print("[M242 REBOOT DISK PASS] original Alpine first-boot nonce persisted "
          "in clean physical ext4 after same-QEMU second boot and final poweroff")


if __name__ == "__main__":
    main()
