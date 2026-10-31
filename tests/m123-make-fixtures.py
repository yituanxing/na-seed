#!/usr/bin/env python3
"""Build ext2 and FAT raw-disk inputs on Linux HOST; never compile in NaSeed."""

from pathlib import Path
import subprocess
import sys

EXT_BYTES = 8 * 1024 * 1024
FAT_BYTES = 4 * 1024 * 1024


def blank(path: Path, size: int) -> None:
    # Small raw images: deterministic zero-filled logical blocks.
    with path.open("wb") as out:
        out.truncate(size)


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: m123-make-fixtures.py ROOTFS_TESTS_DIR")
    root = Path(sys.argv[1])
    root.mkdir(parents=True, exist_ok=True)

    ext = root / "m123-ext2.img"
    blank(ext, EXT_BYTES)
    subprocess.run(
        ["mke2fs", "-q", "-F", "-t", "ext2", "-b", "4096",
         "-L", "M123_BASE", str(ext)],
        check=True,
    )
    with ext.open("rb") as inp:
        inp.seek(1024 + 56)
        assert inp.read(2) == b"\x53\xef", "missing real ext2 superblock"

    blank(root / "m123-empty-ext.img", EXT_BYTES)
    blank(root / "m123-empty-fat.img", FAT_BYTES)
    print("[M123 HOST FS PASS] formatted 8MiB ext2 plus raw ext/FAT disk images")


if __name__ == "__main__":
    main()
