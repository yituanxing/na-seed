#!/bin/sh
# The SAME unmodified C source runs as a native Linux oracle on all
# three ISAs before any NaSeed full-kernel compatibility claim.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 amd64|riscv64|arm64" >&2; exit 2; }
case "$1" in
  amd64) cc=gcc;emu= ;;
  riscv64) cc=riscv64-linux-gnu-gcc;emu=qemu-riscv64 ;;
  arm64) cc=aarch64-linux-gnu-gcc;emu=qemu-aarch64 ;;
  *) exit 2 ;;
esac
out="build/source-busybox/$1/m30-signal"
mkdir -p "$(dirname "$out")"
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
  tests/m30-signal-handlers.c -o "$out"
! readelf -l "$out" | grep -q INTERP
if [ -n "$emu" ]; then output=$("$emu" "$out")
else output=$("$out"); fi
test "$output" = m30-signal-ok
echo "[M30 LINUX ORACLE PASS] $1 sigaction/rt_sigreturn/masks/EINTR"
