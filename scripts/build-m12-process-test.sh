#!/bin/sh
# M12: one identical standard C Linux-ABI process test, real target libc.
# Linux/QEMU-user is the external oracle; full NaSeed native boot follows.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: build-m12-process-test.sh amd64|riscv64|arm64' >&2; exit 2; }
case "$1" in
    amd64) cc=gcc; qemu= ;;
    riscv64) cc=riscv64-linux-gnu-gcc; qemu=qemu-riscv64 ;;
    arm64) cc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
    *) exit 2 ;;
esac
out="build/source-busybox/$1/m12-process"
mkdir -p "$(dirname "$out")"
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m12-process-lifecycle.c -o "$out"
if readelf -l "$out" | grep -q INTERP; then
    echo '[M12] test must be static target ELF' >&2
    exit 1
fi
if [ -n "$qemu" ]; then
    actual=$(LC_ALL=C "$qemu" "$out")
else
    actual=$(LC_ALL=C "$out")
fi
[ "$actual" = m12-process-ok ] || {
    echo "[M12 LINUX ORACLE FAIL] $1: $actual" >&2
    exit 1
}
echo "[M12 LINUX ORACLE PASS] $1 original C process/wait/pipe/orphan lifecycle"
