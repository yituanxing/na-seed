#!/usr/bin/env python3
"""Generate real Netpbm and PNG input plus exact pixel oracles on Linux host."""

import pathlib
import struct
import sys
import zlib


SIZE = 4
RGB = [
    (x * 61 + y * 11, y * 55 + x * 7, x * 23 + y * 37)
    for y in range(SIZE)
    for x in range(SIZE)
]


def ppm(width: int, height: int, pixels: list[tuple[int, int, int]], maxval: int = 255) -> bytes:
    assert len(pixels) == width * height
    assert all(0 <= v <= maxval for rgb in pixels for v in rgb)
    return (f"P6\n{width} {height}\n{maxval}\n".encode("ascii")
            + bytes(value for rgb in pixels for value in rgb))


def pgm(width: int, height: int, pixels: list[int], maxval: int = 255) -> bytes:
    assert len(pixels) == width * height
    return (f"P5\n{width} {height}\n{maxval}\n".encode("ascii") + bytes(pixels))


def chunk(tag: bytes, data: bytes) -> bytes:
    return (struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def png(width: int, height: int, pixels: list[tuple[int, int, int]]) -> bytes:
    rows = bytearray()
    for y in range(height):
        rows.append(0)  # PNG row filter: none
        for pixel in pixels[y * width:(y + 1) * width]:
            rows.extend(pixel)
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(rows), 9))
            + chunk(b"IEND", b""))


def make(root: pathlib.Path) -> None:
    root.mkdir(parents=True, exist_ok=True)
    red = [(255, 0, 0)] * 16
    blue = [(0, 0, 255)] * 16
    golden = {
        "original.ppm": ppm(4, 4, RGB),
        "red.ppm": ppm(4, 4, red),
        "blue.ppm": ppm(4, 4, blue),
        "black.pbm": b"P4\n4 4\n" + (b"\xf0" * 4),
        "white.pgm": pgm(4, 4, [255] * 16),
        "flipped.ppm": ppm(4, 4, [RGB[y * 4 + (3 - x)]
                                   for y in range(4) for x in range(4)]),
        "cropped.ppm": ppm(2, 2, [RGB[y * 4 + x]
                                   for y in range(1, 3) for x in range(1, 3)]),
        "inverted.ppm": ppm(4, 4, [tuple(255 - v for v in pixel) for pixel in RGB]),
        "red-channel.pgm": pgm(4, 4, [pixel[0] for pixel in RGB]),
        "red8.ppm": ppm(8, 8, [(255, 0, 0)] * 64),
        "red31.ppm": ppm(4, 4, [(31, 0, 0)] * 16, 31),
        "red-blue.ppm": ppm(8, 4, [p for _ in range(4) for p in
                                         ([(255, 0, 0)] * 4 + [(0, 0, 255)] * 4)]),
        "original.png": png(4, 4, RGB),
        "invalid.ppm": b"P6\n4 4\n255\n" + b"\x01\x02\x03",  # truncated raster
    }
    for name, value in golden.items():
        (root / f"m122-{name}").write_bytes(value)
    assert len(golden) == 14
    print("[M122 HOST PNM PASS] 14 host image fixtures and exact golden pixel oracles")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: m122-make-fixtures.py ROOTFS_TESTS_DIR")
    make(pathlib.Path(sys.argv[1]))
