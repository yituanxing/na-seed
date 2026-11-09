#!/usr/bin/env python3
"""Independent host evidence for guest-installed signed Alpine APK + OpenRC.

The original signed baseline is read-only. All persisted world/package and
runlevel mutations must originate inside one true two-boot QEMU guest.
"""

import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile


WITNESS = b"M243_ORIGINAL_APK_OPENRC_REBOOT_20261109\n"
WITNESS_PATH = "/root/m243-cli-witness.txt"
RUNLEVEL_LINK = "/etc/runlevels/default/crond"


def import_sibling(name: str, alias: str):
    path = Path(__file__).with_name(name)
    spec = importlib.util.spec_from_file_location(alias, path)
    assert spec and spec.loader
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


def verify(port: str, signed_root: Path, disk: Path) -> None:
    m191 = import_sibling("m191-alpine-root-disk.py", "m243_m191_disk")
    m195 = import_sibling("m195-alpine-disk-check.py", "m243_m195_disk")
    signed_root = signed_root.resolve(strict=True)
    disk = disk.resolve(strict=True)
    original = m191.guest_path(signed_root, RUNLEVEL_LINK)
    if original.exists() or original.is_symlink():
        raise RuntimeError("[M243 HOST FAIL] original Alpine pre-enabled crond; "
                           "cannot attribute runlevel link to guest rc-update")
    m195.verify(port, signed_root, disk, stage="M243", witness_path=WITNESS_PATH,
                witness=WITNESS, packages=("tree", "jq"),
                executables=("/usr/bin/tree", "/usr/bin/jq"))
    with tempfile.TemporaryDirectory(prefix="m243-cli-runlevel-") as temp:
        partition = Path(temp) / "root.ext4"
        m191.extract_partition(disk, partition)
        symlink = subprocess.run(["debugfs", "-R", "stat " + RUNLEVEL_LINK,
                                  str(partition)], check=True, capture_output=True,
                                 text=True)
        if ("Type: symlink" not in symlink.stdout or
                "crond" not in symlink.stdout):
            raise RuntimeError("[M243 HOST FAIL] actual guest OpenRC runlevel "
                               "symlink absent or changed: " + symlink.stdout[-500:])
    print("[M243 ORIGINAL ALPINE CLI DISK PASS] verified official apk tree+jq "
          "world/installed DB/native ELF, guest-created OpenRC default crond "
          "symlink, first-boot witness and clean physical ext4 after SAME-QEMU reboot")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("usage: m243-verify-cli-disk.py ISA ORIGINAL_SIGNED_ROOT IMG")
    verify(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]))
