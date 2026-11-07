#!/bin/sh
# Host-only portable PNM data fixtures for thirty original Alpine Netpbm ELFs.
# Neither native code nor a host-built codec is injected into NaSeed.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
root=$2
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
case "$root" in build/external/m158-batch/*/rootfs) ;; *) exit 2 ;; esac
mkdir -p "$root/tests"
python3 - "$root/tests" <<'PY'
from pathlib import Path
import sys

dest = Path(sys.argv[1])
# A full 64-color PPM catches spurious monochrome/empty converters.
color = b"P3\n8 8\n255\n" + b"".join(
    ("%d %d %d\n" % (x * 32, y * 32, (x + y) * 16)).encode("ascii")
    for y in range(8) for x in range(8)
)
(dest / "m158-color.ppm").write_bytes(color)

# Atari NEO and Degas PI1 are fixed 320x200 4-bit indexed formats.
# Only two palette colors are needed, and both files carry real image data.
(dest / "m158-palette.ppm").write_bytes(
    b"P3\n8 8\n255\n" + b"".join(
        b"255 0 0\n" if (x + y) % 2 else b"0 0 255\n"
        for y in range(8) for x in range(8)
    )
)
(dest / "m158-gray.pgm").write_bytes(
    b"P2\n8 8\n255\n" + b"".join(
        (" ".join(str((x * 20 + y * 11) % 256) for x in range(8))
         + "\n").encode("ascii") for y in range(8)
    )
)
(dest / "m158-bw.pbm").write_bytes(
    b"P1\n8 8\n" + b"".join(
        (" ".join(str((x + y) % 2) for x in range(8))
         + "\n").encode("ascii") for y in range(8)
    )
)
PY
for input in m158-color.ppm m158-palette.ppm m158-gray.pgm m158-bw.pbm; do
    test -s "$root/tests/$input" || exit 1
done
echo "[M158 HOST FIXTURES PASS] $port 64-color, 2-color, gray and bi-level PNM"
