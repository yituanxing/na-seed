#!/bin/sh
# Genuine standard Linux-ABI PID1 binary for each native NaSeed architecture.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: build-m13-init.sh amd64|riscv64|arm64' >&2; exit 2; }
case "$1" in
    amd64) cc=gcc; qemu= ;;
    riscv64) cc=riscv64-linux-gnu-gcc; qemu=qemu-riscv64 ;;
    arm64) cc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
    *) echo "unsupported M13 ISA $1" >&2; exit 2 ;;
esac
out="build/source-busybox/$1/m13-init"
mkdir -p "$(dirname "$out")"
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m13-init.c -o "$out"
if readelf -l "$out" | grep -q INTERP; then
    echo '[M13] PID1 must be a standalone static target ELF' >&2; exit 1
fi
if [ -n "$qemu" ]; then
    actual=$(LC_ALL=C "$qemu" "$out" --linux-oracle)
else
    actual=$(LC_ALL=C "$out" --linux-oracle)
fi
[ "$actual" = m13-init-ok ] || {
    echo "[M13 LINUX ORACLE FAIL] $1: $actual" >&2; exit 1;
}
echo "[M13 LINUX ORACLE PASS] $1 original Linux-ABI init fork-exec-wait"
