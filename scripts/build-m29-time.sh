#!/bin/sh
# Same POSIX/Linux timespec/poll oracle on host Linux or QEMU-user.
# The exact ELF bytes are run by NaSeed system QEMU from the CPIO archive.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 amd64|riscv64|arm64" >&2; exit 2; }
case "$1" in
  amd64) cc=gcc;emu= ;;
  riscv64) cc=riscv64-linux-gnu-gcc;emu=qemu-riscv64 ;;
  arm64) cc=aarch64-linux-gnu-gcc;emu=qemu-aarch64 ;;
  *) exit 2 ;;
esac
out="build/source-busybox/$1/m29-time"
mkdir -p "$(dirname "$out")"
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m29-time.c -o "$out"
! readelf -l "$out" | grep -q INTERP
if [ -n "$emu" ]; then
  output=$("$emu" "$out")
else
  output=$("$out")
fi
test "$output" = m29-time-ok
echo "[M29 LINUX ORACLE PASS] $1 monotonic/sleep/poll"
