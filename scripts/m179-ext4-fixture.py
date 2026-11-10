#!/usr/bin/env python3
"""M179: independently seed/inspect a genuine partitioned VirtIO disk.

The separate M178 16 MiB disk retains its unpartitioned block test at LBAs
16/17 and 32/33. MBR partition #1 starts at LBA4096, so the M178 tests and
this real lwext4 filesystem cannot overwrite each other's data.

This script deliberately uses host e2fsprogs to create and inspect the actual
disk image. It never treats a guest serial PASS marker as proof of durability.
"""

import os
import pathlib
import struct
import subprocess
import sys
import tempfile

SECTOR = 512
DISK_SIZE = 16 * 1024 * 1024
PART_START = 4096
PART_SECTORS = DISK_SIZE // SECTOR - PART_START
SEED_PATH = "/host-seed.bin"
GUEST_PATH = "/guest-write.bin"
SEED_SIZE = 8192
GUEST_SIZE = 12288


def payload(host: bool) -> bytes:
    prefix = b"M179_HOST_EXT4_SEED\0" if host else b"M179_GUEST_EXT4_WRITE\0"
    size = SEED_SIZE if host else GUEST_SIZE
    return prefix + bytes((i * 31 + (17 if host else 43)) & 255
                          for i in range(size - len(prefix)))


def debugfs_cat(partition: pathlib.Path, file_path: str) -> bytes:
    return subprocess.run(
        ["debugfs", "-R", "cat " + file_path, str(partition)],
        check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    ).stdout


def extract_partition(disk: pathlib.Path, destination: pathlib.Path) -> None:
    with disk.open("rb") as source, destination.open("wb") as target:
        source.seek(PART_START * SECTOR)
        left = PART_SECTORS * SECTOR
        while left:
            chunk = source.read(min(left, 1024 * 1024))
            if not chunk:
                raise ValueError("short partition while extracting")
            target.write(chunk)
            left -= len(chunk)


def verify_mbr(disk: pathlib.Path) -> None:
    with disk.open("rb") as raw:
        if os.fstat(raw.fileno()).st_size != DISK_SIZE:
            raise ValueError("M179 disk must be exactly 16 MiB")
        mbr = raw.read(SECTOR)
    expected = struct.pack("<II", PART_START, PART_SECTORS)
    if (mbr[510:512] != b"\x55\xaa" or
            mbr[446 + 4] != 0x83 or
            mbr[446 + 8:446 + 16] != expected or
            mbr[462:510] != bytes(48)):
        raise ValueError("missing/incorrect physical MBR partition geometry")


def prepare(disk: pathlib.Path, temp: pathlib.Path) -> None:
    with disk.open("r+b") as raw:
        if os.fstat(raw.fileno()).st_size != DISK_SIZE:
            raise ValueError("wrong original M178 disk geometry")
        saved = {}
        for sector in (16, 17, 32, 33):
            raw.seek(sector * SECTOR)
            saved[sector] = raw.read(SECTOR)
        mbr = bytearray(SECTOR)
        # A primary Linux partition, starting at 2 MiB. No GPT secondary
        # header/entry overlap with M178's reserved raw-sector oracles.
        mbr[446:462] = (bytes((0, 0, 2, 0, 0x83, 0xFE, 0xFF, 0xFF)) +
                        struct.pack("<II", PART_START, PART_SECTORS))
        mbr[510:512] = b"\x55\xaa"
        raw.seek(0)
        raw.write(mbr)
    partition = temp / "m179-partition.ext4"
    with partition.open("wb") as part:
        part.truncate(PART_SECTORS * SECTOR)
    subprocess.run(
        ["mke2fs", "-q", "-F", "-t", "ext4", "-b", "4096",
         "-O", "^metadata_csum,^64bit", str(partition)], check=True,
    )
    seed = temp / "host-seed.bin"
    seed.write_bytes(payload(True))
    subprocess.run(
        ["debugfs", "-w", "-R", f"write {seed} {SEED_PATH}",
         str(partition)], check=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if debugfs_cat(partition, SEED_PATH) != payload(True):
        raise ValueError("host-seeded ext4 data failed independent readback")
    with disk.open("r+b") as raw, partition.open("rb") as part:
        raw.seek(PART_START * SECTOR)
        while chunk := part.read(1024 * 1024):
            raw.write(chunk)
        for sector, original in saved.items():
            raw.seek(sector * SECTOR)
            if raw.read(SECTOR) != original:
                raise ValueError(f"M178 pre-existing sector {sector} changed")
    verify_mbr(disk)
    print("[M179 HOST SEED PASS] real 16MiB disk, MBR partition LBA4096, "
          "14MiB ext4, 8192-byte host seed, M178 raw sectors retained")


def verify(disk: pathlib.Path, temp: pathlib.Path) -> None:
    verify_mbr(disk)
    partition = temp / "m179-after.ext4"
    extract_partition(disk, partition)
    for path, expected in ((SEED_PATH, payload(True)),
                           (GUEST_PATH, payload(False))):
        actual = debugfs_cat(partition, path)
        if actual != expected:
            mismatch = next((index for index, (a, b) in
                             enumerate(zip(actual, expected)) if a != b),
                            min(len(actual), len(expected)))
            raise ValueError(f"persistent ext4 {path} differs at offset "
                             f"{mismatch}; got {len(actual)} bytes, "
                             f"expected {len(expected)}")
    # The repository's checker inspects BOTH e2fsck's status and its text:
    # the utility can print a free-block/inode count mismatch yet return 0.
    subprocess.run(["sh", "scripts/check-ext4-fsck-clean.sh",
                    str(partition)], check=True, stdout=subprocess.PIPE,
                   stderr=subprocess.PIPE)
    print("[M179 HOST EXT4 PASS] actual QEMU raw disk MBR ext4: original "
          "8192-byte seed intact, 12288-byte guest-created file durable, "
          "read-only e2fsck clean")


def main() -> None:
    if len(sys.argv) != 3 or sys.argv[1] not in ("prepare", "verify"):
        raise SystemExit("usage: m179-ext4-fixture.py prepare|verify DISK")
    disk = pathlib.Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="m179-ext4-") as work:
        temp = pathlib.Path(work)
        (prepare if sys.argv[1] == "prepare" else verify)(disk, temp)


if __name__ == "__main__":
    main()
