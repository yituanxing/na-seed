#!/bin/sh
# Deterministic, host-generated color fixture; original Alpine ImageMagick
# performs the image decode, pixel conversion, and framebuffer write in guest.
set -eu
[ "$#" -eq 2 ] || exit 2
case "$1:$2" in
    amd64:build/external/m161-batch/amd64/rootfs|\
    riscv64:build/external/m161-batch/riscv64/rootfs|\
    arm64:build/external/m161-batch/arm64/rootfs) ;;
    *) exit 2 ;;
esac
mkdir -p "$2/tests"
python3 - "$2/tests/m161-stripes.ppm" <<'PY'
from pathlib import Path
import sys

bands = ((255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255))
pixels = bytes(component for band in bands for _ in range(128) for component in band)
assert len(pixels) == 512 * 3
Path(sys.argv[1]).write_bytes(b'P6\n512 1\n255\n' + pixels)
PY
test -s "$2/tests/m161-stripes.ppm"
echo '[M161 HOST FIXTURE PASS] 512 authentic PPM pixels in four exact color bands'
