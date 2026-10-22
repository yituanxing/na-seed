#!/bin/sh
# One genuine static glibc/Linux ELF across x86, RISC-V64 and AArch64.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: build-m24-signals.sh amd64|riscv64|arm64' >&2;exit 2; }
case "$1" in
  amd64) cc=gcc;emu= ;;
  riscv64) cc=riscv64-linux-gnu-gcc;emu=qemu-riscv64 ;;
  arm64) cc=aarch64-linux-gnu-gcc;emu=qemu-aarch64 ;;
  *)exit 2;;
esac
out="build/source-busybox/$1/m24-signals"
mkdir -p "$(dirname "$out")"
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m24-signals.c -o "$out"
! readelf -l "$out" | grep -q INTERP
prog="$out --linux-oracle"
if [ -n "$emu" ]; then prog="$emu $prog";fi
outstr=$(LC_ALL=C $prog)
printf '%s\n' "$outstr" | grep -Fxq m24-signals-ok
printf '%s\n' "$outstr" | grep -Fxq '[M24] pending-mask-ok'
printf '%s\n' "$outstr" | grep -Fxq '[M24] ignored-signal-ok'
echo "[M24 LINUX SIGNAL PASS] $1 masked/ignored/default native signals"
