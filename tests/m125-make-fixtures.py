#!/usr/bin/env python3
"""Independent Netpbm M125 images and pixel oracles, built on Linux host."""

from pathlib import Path
import sys


def ppm(width: int, height: int, pixels: list[tuple[int, int, int]]) -> bytes:
    assert len(pixels) == width * height
    assert all(0 <= c <= 255 for pixel in pixels for c in pixel)
    return f"P6\n{width} {height}\n255\n".encode() + bytes(
        c for pixel in pixels for c in pixel
    )


def pgm(width: int, height: int, pixels: list[int], maxval: int = 255) -> bytes:
    assert len(pixels) == width * height
    assert all(0 <= c <= maxval for c in pixels)
    return f"P5\n{width} {height}\n{maxval}\n".encode() + bytes(pixels)


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: m125-make-fixtures.py ROOTFS_TESTS_DIR")
    root = Path(sys.argv[1]); root.mkdir(parents=True, exist_ok=True)
    r, b, m = (255, 0, 0), (0, 0, 255), (255, 0, 255)
    pattern = [(x * 51 + y * 7, y * 61 + x * 9, x * 31 + y * 37)
               for y in range(4) for x in range(4)]
    colors = [r if (x + y) % 2 == 0 else b
              for y in range(4) for x in range(4)]
    gray = [0, 32, 224, 255] * 4
    images = {
        "pattern.ppm": ppm(4, 4, pattern),
        "red.ppm": ppm(4, 4, [r] * 16),
        "blue.ppm": ppm(4, 4, [b] * 16),
        "magenta.ppm": ppm(4, 4, [m] * 16),
        "black.ppm": ppm(4, 4, [(0, 0, 0)] * 16),
        "cyan.ppm": ppm(4, 4, [(0, 255, 255)] * 16),
        "checker.ppm": ppm(4, 4, colors),
        "bw-checker.ppm": ppm(4, 4, [(0, 0, 0) if (x + y) % 2 else (255, 255, 255)
                                         for y in range(4) for x in range(4)]),
        "bw-checker.pgm": pgm(4, 4, [0 if (x + y) % 2 else 255
                                          for y in range(4) for x in range(4)]),
        "gray.pgm": pgm(4, 4, gray),
        "gray-to-red.ppm": ppm(4, 4, [(c, 0, 0) for c in gray]),
        "threshold.pbm": b"P4\n4 4\n" + b"\xc0" * 4,
        "indices.pgm": pgm(4, 4, [(x + y) % 2 for y in range(4) for x in range(4)], 1),
        "lookup.ppm": ppm(2, 1, [r, b]),
        "odd.ppm": ppm(4, 2, pattern[4:8] + pattern[12:16]),
        "ascii.txt": b"    \nMMMM\n",
    }
    for y in range(2):
        for x in range(2):
            pixels = [pattern[row * 4 + col]
                      for row in range(y * 2, y * 2 + 2)
                      for col in range(x * 2, x * 2 + 2)]
            images[f"tile_{y}_{x}.ppm"] = ppm(2, 2, pixels)
    for name, content in images.items():
        (root / f"m125-{name}").write_bytes(content)
        assert (root / f"m125-{name}").stat().st_size == len(content)
    assert len(images) == 20
    print("[M125 HOST IMAGE PASS] 20 original-format image and text inputs, exact independent pixel oracles")


if __name__ == "__main__":
    main()
