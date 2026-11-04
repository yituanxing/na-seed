#!/usr/bin/env python3
"""Verify real Alpine rootfs persistence after a *clean* second guest boot.

The M194 witness must be created by the guest on its disposable physical
ext4 disk during an earlier, independent QEMU boot.  The host staging tree
is never allowed to contain that file.  This check requires M191's entire
strict disk and fsck contract, then reads the guest-created file directly
from the actual ext4 partition; QEMU serial text is not disk evidence.
"""

import importlib.util
from pathlib import Path
import re
import sys
import tempfile


WITNESS_PATH = "/root/m194-persistent.txt"
WITNESS_TOKEN = re.compile(r"[A-Za-z0-9._-]{12,120}\Z", re.ASCII)


def load_m191():
    path = Path(__file__).with_name("m191-alpine-root-disk.py")
    spec = importlib.util.spec_from_file_location("naseed_m191_disk", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load M191 disk verification model")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def verify(port: str, root: Path, disk: Path, token: str) -> None:
    m191 = load_m191()
    if port not in m191.ARCH_MACHINE:
        raise ValueError("[M194 HOST FAIL] unknown target ISA")
    if WITNESS_TOKEN.fullmatch(token) is None:
        raise ValueError("[M194 HOST FAIL] invalid witness token")
    if root.is_symlink() or disk.is_symlink():
        raise ValueError("[M194 HOST FAIL] staged root or disk is a host symlink")
    root = root.resolve(strict=True)
    disk = disk.resolve(strict=True)
    if not root.is_dir() or not disk.is_file():
        raise ValueError("[M194 HOST FAIL] invalid staged root or physical disk")

    # If the file were present before QEMU, a host-staged witness could be
    # replayed without any real guest write.  Refuse that false positive.
    # Resolve only the parent. A dangling preseeded leaf symlink must fail
    # as surely as an ordinary file, rather than silently resolving away.
    staged = m191.guest_path(root, "/root") / "m194-persistent.txt"
    if staged.exists() or staged.is_symlink():
        raise ValueError("[M194 HOST FAIL] witness was preseeded in staged rootfs")

    # M191.verify covers MBR geometry, official Alpine/arch identity,
    # package metadata readback, full e2fsck -fn and allocation counts.
    # The additional guest-created witness does not affect those bytes.
    m191.verify(port, root, disk)
    with tempfile.TemporaryDirectory(prefix="m194-disk-readback-") as directory:
        partition = Path(directory) / "root.ext4"
        m191.extract_partition(disk, partition)
        actual = m191.debugfs_cat(partition, WITNESS_PATH)

    expected = (token + "\n").encode("ascii")
    if actual != expected:
        raise ValueError(
            "[M194 HOST FAIL] guest-created ext4 witness missing or changed "
            f"(actual={len(actual)} bytes, expected={len(expected)} bytes)"
        )
    print(f"[M194 ALPINE PERSISTENCE HOST PASS] {port} exact guest ext4 "
          "witness after second boot, MBR and official Alpine APK metadata "
          "unchanged, e2fsck clean")


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit("usage: m194-alpine-disk-check.py PORT ROOT DISK WITNESS_TOKEN")
    verify(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), sys.argv[4])


if __name__ == "__main__":
    main()
