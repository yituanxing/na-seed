#!/usr/bin/env python3
"""M180: independent host persistence oracle for real ring3 ext4 syscalls.

No guest PASS text is trusted as disk durability.  M178 freshly creates a
16MiB physical VirtIO image, M179 formats its real MBR ext4 partition, and
the new M180 user ELF writes `/async/guest-user.bin` through Linux syscalls.
This host script extracts that *raw QEMU disk* after guest clean shutdown,
checks all old fixture payloads and the new exact 12KiB contents via debugfs,
then runs the existing strict read-only e2fsck oracle.
"""

import pathlib
import runpy
import subprocess
import sys
import tempfile


OUTPUT_PATH = "/guest-user.bin"
OUTPUT_BYTES = 12288
OUTPUT_PREFIX = b"M180_USER_EXT4_SYSCALL\0"


def expected_payload() -> bytes:
    return OUTPUT_PREFIX + bytes(
        (offset * 37 + 97) & 255
        for offset in range(OUTPUT_BYTES - len(OUTPUT_PREFIX))
    )


def matching_bytes(actual: bytes, expected: bytes, path: str) -> None:
    if actual == expected:
        return
    different = next(
        (offset for offset, (found, wanted) in
         enumerate(zip(actual, expected)) if found != wanted),
        min(len(actual), len(expected)),
    )
    raise ValueError(
        f"host ext4 {path}: mismatched byte offset {different}; "
        f"actual_size={len(actual)}, expected_size={len(expected)}"
    )


def verify(disk: pathlib.Path) -> None:
    # Reuse M179's physical partition geometry and payload specification
    # instead of drifting into an unrelated synthetic filesystem oracle.
    m179 = runpy.run_path(str(pathlib.Path(__file__).with_name(
        "m179-ext4-fixture.py")))
    m179["verify_mbr"](disk)
    with tempfile.TemporaryDirectory(prefix="m180-ext4-host-") as work:
        partition = pathlib.Path(work) / "m180-after-qemu.ext4"
        m179["extract_partition"](disk, partition)
        old_payloads = (
            (m179["SEED_PATH"], m179["payload"](True)),
            (m179["GUEST_PATH"], m179["payload"](False)),
        )
        for path, payload in old_payloads:
            matching_bytes(m179["debugfs_cat"](partition, path),
                           payload, path)
        matching_bytes(m179["debugfs_cat"](partition, OUTPUT_PATH),
                       expected_payload(), OUTPUT_PATH)
        # ext4 journal/superblock consistency is an additional oracle:
        # Linux read(2) success followed by fsync(2) does not alone prove
        # the final upstream ext4_umount and durable BlockDevice FLUSH.
        subprocess.run(["sh", "scripts/check-ext4-fsck-clean.sh",
                        str(partition)], check=True, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)

    print("[M180 HOST EXT4 USER PERSISTENCE PASS] post-QEMU raw 16MiB "
          "VirtIO disk with real MBR partition: original 8192-byte host "
          "seed, M179 12288-byte kernel-worker file, separate M180 "
          "12288-byte ring3 Linux-syscall file match byte-for-byte; "
          "read-only e2fsck clean")


def main() -> None:
    if len(sys.argv) != 3 or sys.argv[1] != "verify":
        raise SystemExit("usage: m180-ext4-fixture.py verify DISK")
    verify(pathlib.Path(sys.argv[2]).resolve())


if __name__ == "__main__":
    main()
