#!/bin/sh
set -eu
[ "$#" -eq 1 ] || { echo 'usage: build-m21-tty-sched.sh amd64|riscv64|arm64' >&2;exit 2; }
case "$1" in
  amd64) cc=gcc;qemu= ;;
  riscv64) cc=riscv64-linux-gnu-gcc;qemu=qemu-riscv64 ;;
  arm64) cc=aarch64-linux-gnu-gcc;qemu=qemu-aarch64 ;;
  *)exit 2;;
esac
out="build/source-busybox/$1/m21-tty-sched"
mkdir -p "$(dirname "$out")"
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m21-tty-sched.c -o "$out"
! readelf -l "$out" | grep -q INTERP
command -v script >/dev/null
prog="$out --linux-pty-oracle"
if [ -n "$qemu" ]; then prog="$qemu $prog";fi
output=$(script -q -e -c "$prog" /dev/null | tr -d '\r')
printf '%s\n' "$output" | grep -Fxq m21-linux-pty-ok
echo "[M21 LINUX PTY PASS] $1 native ELF"
