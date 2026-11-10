#!/usr/bin/env python3
"""Create deterministic, lossless input data for unmodified Alpine fbi.

This is a *picture*, never a guest executable or generated framebuffer. The
only accepted pixels are later read independently from QEMU's physical QMP.
"""

import argparse
import hashlib
from pathlib import Path
import struct
import zlib


WIDTH = 800
HEIGHT = 600
COLORS = (b"\xff\x00\x00", b"\x00\xff\x00", b"\x00\x00\xff", b"\xff\xff\xff")


def chunk(name: bytes, payload: bytes) -> bytes:
    return (struct.pack(">I", len(payload)) + name + payload +
            struct.pack(">I", zlib.crc32(name + payload) & 0xffffffff))


def make_png() -> bytes:
    # Each channel of every 200px bar is exactly 0/255; no antialiasing,
    # embedded color profiles, alpha, scaling or text overlays.
    row = b"\x00" + b"".join(color * 200 for color in COLORS)
    body = zlib.compress(row * HEIGHT, level=9)
    ihdr = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", body) + chunk(b"IEND", b""))


def main() -> None:
    cli = argparse.ArgumentParser()
    cli.add_argument("output", type=Path)
    args = cli.parse_args()
    png = make_png()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(png)
    print(f"[M201 PNG FIXTURE] 800x600 original input RGB bars; sha256="
          f"{hashlib.sha256(png).hexdigest()} path={args.output}")


if __name__ == "__main__":
    main()
