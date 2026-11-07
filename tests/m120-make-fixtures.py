#!/usr/bin/env python3
"""Host-only deterministic RGB, grayscale and raw image fixtures for LibTIFF.

This is run by the signed APK host staging hook, never by the NaSeed guest.
"""

import sys
from pathlib import Path


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: m120-make-fixtures.py ROOT_TESTS")
    dest = Path(sys.argv[1])
    dest.mkdir(parents=True, exist_ok=True)
    width = height = 16
    rgb = bytearray()
    inverted = bytearray()
    grayscale = bytearray()
    for y in range(height):
        for x in range(width):
            r = (17 * x + 7 * y) % 256
            g = (11 * x + 23 * y + 13) % 256
            b = (19 * x + 3 * y + 71) % 256
            rgb.extend((r, g, b))
            inverted.extend((255 - r, 255 - g, 255 - b))
            grayscale.append((17 * x + 13 * y) % 256)
    for filename, pixels in (
        ("m120-color.ppm", rgb),
        ("m120-inverted.ppm", inverted),
    ):
        (dest / filename).write_bytes(b"P6\n16 16\n255\n" + pixels)
    (dest / "m120-gray.pgm").write_bytes(b"P5\n16 16\n255\n" + grayscale)
    (dest / "m120-gray.raw").write_bytes(grayscale)
    assert len(rgb) == 768 and len(grayscale) == 256


if __name__ == "__main__":
    main()
