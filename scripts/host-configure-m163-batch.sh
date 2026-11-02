#!/bin/sh
# Four true image/color fixtures, created on the host; all 33 ELF files
# remain unchanged and come exclusively from signed Alpine v3.23 APKs.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
root=$2
case "$port:$root" in
    amd64:build/external/m163-batch/amd64/rootfs|\
    riscv64:build/external/m163-batch/riscv64/rootfs|\
    arm64:build/external/m163-batch/arm64/rootfs) ;;
    *) exit 2 ;;
esac
python3 - "$root/tests" <<'PY'
from pathlib import Path
import sys

target = Path(sys.argv[1])
target.mkdir(parents=True, exist_ok=True)
target.joinpath('m163-color.ppm').write_bytes(
    b'P3\n8 8\n255\n' + b''.join(
        f'{x * 32} {y * 32} {(x + y) * 16}\n'.encode()
        for y in range(8) for x in range(8)))
target.joinpath('m163-gray.pgm').write_bytes(
    b'P2\n8 8\n255\n' + b''.join(
        f'{(x * 29 + y * 13) % 256}\n'.encode()
        for y in range(8) for x in range(8)))
target.joinpath('m163-bw.pbm').write_bytes(
    b'P1\n8 8\n' + b''.join(
        f'{(x + y) % 2}\n'.encode()
        for y in range(8) for x in range(8)))
target.joinpath('m163-rgb.txt').write_text(
    '0 0 0 black\n255 255 255 white\n128 128 128 gray\n', encoding='ascii')
PY
for name in color.ppm gray.pgm bw.pbm rgb.txt; do
    test -s "$root/tests/m163-$name"
done
echo "[M163 HOST FIXTURES PASS] $port RGB/gray/bi-level images and color dictionary"
