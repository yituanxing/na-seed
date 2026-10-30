#!/bin/sh
# Generate data on the Linux host; no compilation inside NaSeed's QEMU guest.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
root=$2
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac
command -v python3 >/dev/null
python3 tests/m120-make-fixtures.py "$root/tests"
for file in m120-color.ppm m120-inverted.ppm m120-gray.pgm m120-gray.raw; do
    test -s "$root/tests/$file" || exit 1
done
test "$(wc -c < "$root/tests/m120-gray.raw")" -eq 256
echo "[M120 HOST DATA PASS] $port 16x16 RGB and grayscale, no guest build"
