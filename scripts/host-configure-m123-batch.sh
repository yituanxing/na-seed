#!/bin/sh
# Only Linux host creates raw image fixtures; NaSeed merely runs native tools.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
root=$2
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac
command -v python3 >/dev/null
command -v mke2fs >/dev/null
python3 tests/m123-make-fixtures.py "$root/tests"
for img in ext2 empty-ext empty-fat; do
    test -s "$root/tests/m123-$img.img" || exit 1
done
echo "[M123 HOST DATA PASS] $port real ext2 image and writeable blank disks"
