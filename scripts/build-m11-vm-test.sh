#!/bin/sh
# Same original C program compiled for three Linux-ABI user targets.
# Run under real Linux QEMU-user first, then full NaSeed QEMU. No fake ABI.
set -eu
[ "$#" -eq 1 ] || exit 2
case "$1" in
    amd64) cc=gcc; qemu= ;;
    riscv64) cc=riscv64-linux-gnu-gcc; qemu=qemu-riscv64 ;;
    arm64) cc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
    *) exit 2 ;;
esac
out="build/source-busybox/$1/m11-vm"
mkdir -p "$(dirname "$out")"
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m11-vm-lifecycle.c -o "$out"
if readelf -l "$out" | grep -q INTERP; then
    echo '[M11] test binary must be static' >&2
    exit 1
fi
if [ -n "$qemu" ]; then
    actual=$(LC_ALL=C "$qemu" "$out")
else
    actual=$(LC_ALL=C "$out")
fi
[ "$actual" = m11-vm-ok ] || {
    echo "[M11 LINUX ORACLE FAIL] $1: $actual" >&2; exit 1;
}
echo "[M11 LINUX ORACLE PASS] $1 standard static libc process/VM lifecycle"
