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
