#!/bin/sh
# Prepare real binary disk and tar/source-tree fixtures on the Linux host.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
root=$2
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac
[ "$root" = "build/external/m128-batch/$port/rootfs" ] || exit 2
python3 tests/m128-make-fixtures.py "$root/tests"
test -s "$root/tests/m128-input.tar"
test -s "$root/tests/m128-blank-exfat.img"
test -s "$root/tests/m128-tree/alpha.txt"
test -s "$root/tests/m128-tree/nested/beta.txt"
echo "[M128 HOST CONFIG PASS] $port fixed SquashFS tar/tree and blank exFAT image"
