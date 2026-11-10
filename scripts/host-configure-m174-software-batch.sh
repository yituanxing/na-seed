#!/bin/sh
# Host-only profile fixture: preserve the exact staged candidate list inside
# the guest for real entrypoint smoke tests, without changing Alpine ELF bytes.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1 root=$2
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
test -s scripts/m174-software-original-elfs.txt
mkdir -p "$root/tests"
cp scripts/m174-software-original-elfs.txt \
    "$root/tests/m174-software-original-elfs.txt"
# One CPIO, two non-overlapping historical signed-ELF manifest snapshots.
# Profile copy maps intentionally admit only tests/ sources; manifest sources
# are staged here, just like the original M174 signed-candidate manifest.
test -s scripts/m187-original-elfs.txt
cp scripts/m187-original-elfs.txt \
    "$root/tests/m187-original-elfs.txt"
# M188's two new ELFs are additive; M79 Vim and M80 Nano are reused.
test -s scripts/m188-original-elfs.txt
cp scripts/m188-original-elfs.txt \
    "$root/tests/m188-original-elfs.txt"
