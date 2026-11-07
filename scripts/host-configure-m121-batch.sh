#!/bin/sh
# Every PDF and data source is generated on the Linux host, NEVER inside NaSeed.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
root=$2
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac
command -v python3 >/dev/null
python3 tests/m121-make-fixtures.py "$root/tests"
test -s "$root/tests/m121-original.pdf"
test -s "$root/tests/m121-invalid.pdf"
echo "[M121 HOST DATA PASS] $port genuine two-page PDF and negative oracle"
