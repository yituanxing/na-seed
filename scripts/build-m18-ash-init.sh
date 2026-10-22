#!/bin/sh
# One unchanged standard C PID1 for all native Linux 64-bit ISAs.
# Runtime interactive COM1 verification is presently x86-only.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: build-m18-ash-init.sh amd64|riscv64|arm64' >&2; exit 2; }
case "$1" in
    amd64) cc=gcc;qemu= ;;
    riscv64) cc=riscv64-linux-gnu-gcc;qemu=qemu-riscv64 ;;
    arm64) cc=aarch64-linux-gnu-gcc;qemu=qemu-aarch64 ;;
    *) exit 2 ;;
esac
dir="build/source-busybox/$1"
mkdir -p "$dir"
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m18-ash-init.c -o "$dir/m18-ash-init"
if readelf -l "$dir/m18-ash-init" | grep -q INTERP; then
    echo '[M18] native standalone static ELF required' >&2; exit 1
fi
command -v script >/dev/null 2>&1 || exit 2
prog="$dir/m18-ash-init --linux-pty-oracle"
if [ -n "$qemu" ]; then prog="$qemu $prog"; fi
output=$(script -q -e -c "$prog" /dev/null | tr -d '\r')
printf '%s\n' "$output" | grep -Fxq m18-linux-pty-ok || {
    echo "[M18 LINUX PTY ORACLE FAIL] $1 $output" >&2; exit 1
}
echo "[M18 LINUX PTY PASS] $1 native init tty/glibc ELF"
