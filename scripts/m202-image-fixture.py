#!/usr/bin/env python3
"""Host-only second original-fbi PNG input; never a guest rendering oracle."""

import argparse
import hashlib
from pathlib import Path
import struct
import zlib


COLORS = (b'\x00\xff\xff', b'\xff\x00\xff',
          b'\xff\xff\x00', b'\x00\x00\x00')


def chunk(tag: bytes, payload: bytes) -> bytes:
    return (struct.pack('>I', len(payload)) + tag + payload +
            struct.pack('>I', zlib.crc32(tag + payload) & 0xffffffff))


def make_png() -> bytes:
    scanline = b'\x00' + b''.join(color * 200 for color in COLORS)
    ihdr = struct.pack('>IIBBBBB', 800, 600, 8, 2, 0, 0, 0)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) +
            chunk(b'IDAT', zlib.compress(scanline * 600, 9)) +
            chunk(b'IEND', b''))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('output', type=Path)
    output = parser.parse_args().output
    png = make_png()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(png)
    print('[M202 SECOND PNG] independent 800x600 input only, sha256=' +
          hashlib.sha256(png).hexdigest())


if __name__ == '__main__':
    main()
