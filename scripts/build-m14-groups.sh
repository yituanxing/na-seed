#!/bin/sh
# One pinned original C program, native Linux libc under 3 ISAs.
# Linux or QEMU-user first, real native system QEMU later; no fake syscalls.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: build-m14-groups.sh amd64|riscv64|arm64' >&2; exit 2; }
case "$1" in
    amd64) cc=gcc; qemu= ;;
    riscv64) cc=riscv64-linux-gnu-gcc; qemu=qemu-riscv64 ;;
    arm64) cc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
    *) exit 2 ;;
esac
out="build/source-busybox/$1/m14-groups"
mkdir -p "$(dirname "$out")"
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m14-groups.c -o "$out"
if readelf -l "$out" | grep -q INTERP; then
    echo '[M14] C workload must be a static native target ELF' >&2; exit 1;
fi
if [ -n "$qemu" ]; then
    actual=$(LC_ALL=C "$qemu" "$out")
else
    actual=$(LC_ALL=C "$out")
fi
[ "$actual" = m14-groups-ok ] || {
    echo "[M14 LINUX ORACLE FAIL] $1: $actual" >&2; exit 1;
}
echo "[M14 LINUX ORACLE PASS] $1 process groups, sessions and waitpid selectors"
