#!/bin/sh
# Every filesystem fixture is prepared outside NaSeed; guest runs only ELFs.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
root=$2
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac
command -v python3 >/dev/null
python3 tests/m124-make-fixtures.py "$root/tests"
test -s "$root/tests/m124-empty-minix.img"
test -s "$root/tests/m124-empty-swap.img"
test -s "$root/tests/m124-cramfs-source/hello.txt"
test -s "$root/tests/m124-cramfs-source/nested/entry.txt"
test -s "$root/tests/m124-look-words.txt"
echo "[M124 HOST DATA PASS] $port original util-linux inputs generated on Linux host"
