#!/bin/sh
# Host-only signed Alpine Netpbm image inputs; no NaSeed guest build.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
root=$2
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac
python3 tests/m125-make-fixtures.py "$root/tests"
for fixture in pattern.ppm red.ppm blue.ppm magenta.ppm black.ppm cyan.ppm \
               checker.ppm bw-checker.ppm bw-checker.pgm gray.pgm \
               gray-to-red.ppm threshold.pbm indices.pgm lookup.ppm odd.ppm \
               ascii.txt tile_0_0.ppm tile_0_1.ppm tile_1_0.ppm tile_1_1.ppm; do
    test -s "$root/tests/m125-$fixture"
done
echo "[M125 HOST DATA PASS] $port 20 exact input and pixel golden fixtures"
