#!/bin/sh
set -eu
[ "$#" -eq 1 ] || { echo 'usage: build-m23-signals.sh amd64|riscv64|arm64' >&2;exit 2; }
case "$1" in
  amd64) cc=gcc;emu= ;;
  riscv64) cc=riscv64-linux-gnu-gcc;emu=qemu-riscv64 ;;
  arm64) cc=aarch64-linux-gnu-gcc;emu=qemu-aarch64 ;;
  *)exit 2;;
esac
out="build/source-busybox/$1/m23-signals"
mkdir -p "$(dirname "$out")"
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
   tests/m23-signals.c -o "$out"
! readelf -l "$out" | grep -q INTERP
command -v script >/dev/null
cmd="$out --linux-pty-oracle"
if [ -n "$emu" ];then cmd="$emu $cmd";fi
outstr=$(script -q -e -c "$cmd" /dev/null | tr -d '\r')
printf '%s\n' "$outstr" | grep -Fxq m23-linux-pty-ok
echo "[M23 LINUX PTY PASS] $1 native ABI"
