#!/bin/sh
# One standard Linux C source, three native ABI ELF compilers.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: build-m22-poll.sh amd64|riscv64|arm64' >&2; exit 2; }
case "$1" in
  amd64) cc=gcc;emu= ;;
  riscv64) cc=riscv64-linux-gnu-gcc;emu=qemu-riscv64 ;;
  arm64) cc=aarch64-linux-gnu-gcc;emu=qemu-aarch64 ;;
  *)exit 2;;
esac
out="build/source-busybox/$1/m22-poll"
mkdir -p "$(dirname "$out")"
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
  tests/m22-poll.c -o "$out"
! readelf -l "$out" | grep -q INTERP
cmd="$out --linux-pty-oracle"
if [ -n "$emu" ]; then cmd="$emu $cmd";fi
output=$(script -q -e -c "$cmd" /dev/null | tr -d '\r')
printf '%s\n' "$output" | grep -Fxq m22-linux-pty-ok
echo "[M22 LINUX PTY PASS] $1 standard Linux ELF"
