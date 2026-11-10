#!/bin/sh
# Build small, deterministic IMAGE DATA fixtures on the host after signed APK
# import. No host-built native executable is copied into the guest.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
root=$2
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
case "$root" in build/external/m157-batch/*/rootfs) ;; *) exit 2 ;; esac
mkdir -p "$root/tests"
python3 - "$root/tests" <<'PY'
from pathlib import Path
import base64
import struct
import zlib
import sys

dest = Path(sys.argv[1])
pixels = [(x * 32, y * 32, (x + y) * 16) for y in range(8) for x in range(8)]
ppm = b"P3\n8 8\n255\n" + b"".join(
    ("%d %d %d\n" % rgb).encode("ascii") for rgb in pixels
)
(dest / "m157-color.ppm").write_bytes(ppm)
(dest / "m157-bw.pbm").write_text(
    "P1\n8 8\n" + "".join(
        " ".join(str((x + y) % 2) for x in range(8)) + "\n"
        for y in range(8)
    ), encoding="ascii"
)
(dest / "m157-gray.pgm").write_text(
    "P2\n8 8\n255\n" + "".join(
        " ".join(str((x * 20 + y * 11) % 256) for x in range(8)) + "\n"
        for y in range(8)
    ), encoding="ascii"
)

def chunk(name, data, corrupt_len=None):
    length = len(data) if corrupt_len is None else corrupt_len
    crc = zlib.crc32(name + data) & 0xffffffff
    return struct.pack(">I", length) + name + data + struct.pack(">I", crc)

scanlines = b"".join(
    b"\0" + b"".join(bytes(rgb) for rgb in pixels[y*8:y*8+8])
    for y in range(8)
)
head = b"\x89PNG\r\n\x1a\n"
ihdr = chunk(b"IHDR", struct.pack(">IIBBBBB", 8, 8, 8, 2, 0, 0, 0))
itxt = b"Title\x00\x00\x00\x00\x00NaSeed M157"
body = chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b"")
(dest / "m157-color.png").write_bytes(head + ihdr + chunk(b"iTXt", itxt) + body)
# Reproduce an old libpng iTXt length bug, with the CRC left correct so the
# upstream png-fix-itxt utility must repair the original malformed chunk.
(dest / "m157-bad-itxt.png").write_bytes(
    head + ihdr + chunk(b"iTXt", itxt, len(itxt) - 1) + body
)

(dest / "m157-color.gif").write_bytes(base64.b64decode(
    "R0lGODdhBAAEAIEAAAAAAP8AAAD/AAAAACwAAAAABAAEAAAIDgABBBAQQCBBgwUHBggIADs="
))

# Little-endian uncompressed palette TIFF: 4x4 with four distinct entries.
# Keeping the ColorMap as a real 16-bit TIFF array makes pal2rgb perform
# meaningful pixel conversion, not a bare startup/help probe.
palette = [0] * 768
for channel, index in ((0, 1), (1, 2), (2, 3)):
    palette[channel*256 + index] = 65535
raw = bytes([0, 1, 2, 3] * 4)
n = 10
data_offset = 8 + 2 + 12*n + 4
palette_offset = data_offset + len(raw)
tags = [
    (256, 4, 1, 4), (257, 4, 1, 4), (258, 3, 1, 8),
    (259, 3, 1, 1), (262, 3, 1, 3),
    (273, 4, 1, data_offset), (277, 3, 1, 1),
    (278, 4, 1, 4), (279, 4, 1, len(raw)),
    (320, 3, len(palette), palette_offset),
]
tiff = b"II" + struct.pack("<HIH", 42, 8, n)
tiff += b"".join(struct.pack("<HHII", *t) for t in tags)
tiff += struct.pack("<I", 0) + raw
tiff += struct.pack("<%dH" % len(palette), *palette)
(dest / "m157-paletted.tif").write_bytes(tiff)

# Eight all-white rows, uncompressed fax input for fax2tiff -U -X 8.
(dest / "m157-fax.raw").write_bytes(bytes([0xff] * 8))
PY
for fixture in m157-color.ppm m157-bw.pbm m157-gray.pgm \
               m157-color.png m157-bad-itxt.png m157-color.gif \
               m157-paletted.tif m157-fax.raw; do
    test -s "$root/tests/$fixture" || exit 1
done
echo "[M157 HOST FIXTURES PASS] $port native PNM / PNG / GIF / palette TIFF / fax"
