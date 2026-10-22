#!/bin/sh
# Pinned, unchanged C/Linux-ABI signal workload across three ISAs.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: build-m15-kill.sh amd64|riscv64|arm64' >&2; exit 2; }
case "$1" in
    amd64) cc=gcc; qemu= ;;
    riscv64) cc=riscv64-linux-gnu-gcc; qemu=qemu-riscv64 ;;
    arm64) cc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
    *) exit 2 ;;
esac
out="build/source-busybox/$1/m15-kill"
mkdir -p "$(dirname "$out")"
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m15-kill.c -o "$out"
if readelf -l "$out" | grep -q INTERP; then
    echo '[M15] expected a native static ELF, not a fabricated applet' >&2; exit 1
fi
if [ -n "$qemu" ]; then
    actual=$(LC_ALL=C "$qemu" "$out")
else
    actual=$(LC_ALL=C "$out")
fi
[ "$actual" = m15-kill-ok ] || {
    echo "[M15 LINUX ORACLE FAIL] $1: $actual" >&2; exit 1
}
echo "[M15 LINUX ORACLE PASS] $1 SIGKILL/parked process/group/waitpid"
