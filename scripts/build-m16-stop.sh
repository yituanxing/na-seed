#!/bin/sh
# One upstream-compatible C/Linux ABI oracle for all 3 native architectures.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: build-m16-stop.sh amd64|riscv64|arm64' >&2; exit 2; }
case "$1" in
    amd64) cc=gcc; qemu= ;;
    riscv64) cc=riscv64-linux-gnu-gcc; qemu=qemu-riscv64 ;;
    arm64) cc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
    *) exit 2 ;;
esac
out="build/source-busybox/$1/m16-stop"
mkdir -p "$(dirname "$out")"
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie tests/m16-stop.c -o "$out"
if readelf -l "$out" | grep -q INTERP; then
    echo '[M16] must be a native static Linux ELF' >&2; exit 1
fi
if [ -n "$qemu" ]; then
    actual=$(LC_ALL=C "$qemu" "$out")
else
    actual=$(LC_ALL=C "$out")
fi
[ "$actual" = m16-stop-ok ] || {
    echo "[M16 LINUX ORACLE FAIL] $1: $actual" >&2; exit 1
}
echo "[M16 LINUX ORACLE PASS] $1 process groups, STOP/CONT, wait event and PC preservation"
