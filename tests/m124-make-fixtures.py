#!/usr/bin/env python3
"""Create deterministic native util-linux inputs on the Linux HOST only."""

from pathlib import Path
import sys


def make_blank(path: Path, size: int = 4 * 1024 * 1024) -> None:
    with path.open("wb") as out:
        out.truncate(size)


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: m124-make-fixtures.py ROOTFS_TESTS_DIR")
    root = Path(sys.argv[1])
    root.mkdir(parents=True, exist_ok=True)
    make_blank(root / "m124-empty-minix.img")
    make_blank(root / "m124-empty-swap.img")
    tree = root / "m124-cramfs-source"
    (tree / "nested").mkdir(parents=True, exist_ok=True)
    (tree / "hello.txt").write_bytes(b"NASEED_M124_CRAMFS_PAYLOAD\n")
    (tree / "nested" / "entry.txt").write_bytes(b"M124_NESTED_ENTRY\n")
    assert (root / "m124-empty-minix.img").stat().st_size == 4 * 1024 * 1024
    assert (root / "m124-empty-swap.img").stat().st_size == 4 * 1024 * 1024
    print("[M124 HOST FIXTURES PASS] 4MiB blank Minix/swap images and nested cramfs source")


if __name__ == "__main__":
    main()
