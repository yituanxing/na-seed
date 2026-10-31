#!/bin/sh
# Host-only test inputs and golden images; guest receives final ELF and data.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
root=$2
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac
command -v python3 >/dev/null
python3 tests/m122-make-fixtures.py "$root/tests"
for file in original.ppm red.ppm blue.ppm black.pbm white.pgm \
            flipped.ppm cropped.ppm inverted.ppm red-channel.pgm \
            red8.ppm red31.ppm red-blue.ppm original.png invalid.ppm; do
    test -s "$root/tests/m122-$file" || exit 1
done
echo "[M122 HOST DATA PASS] $port genuine PNM and PNG with independent golden pixels"
