#!/bin/sh
# Same source, native Linux x86 oracle then strict static 3ISA guest ELF.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: build-path-first-dirfd-native.sh amd64|riscv64|arm64' >&2;exit 2; }
case "$1" in
  amd64) cc=gcc; isa='Advanced Micro Devices X86-64' ;;
  riscv64) cc=riscv64-linux-gnu-gcc;isa='RISC-V' ;;
  arm64) cc=aarch64-linux-gnu-gcc;isa='AArch64' ;;
  *) exit 2 ;;
esac
out="build/source-busybox/$1/path-first-dirfd-native"
mkdir -p "$(dirname "$out")"
command -v "$cc" >/dev/null
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
  tests/path-first-dirfd-native.c -o "$out"
readelf -h "$out" | grep -Fq "$isa"
if readelf -l "$out" | grep -Fq INTERP;then
  echo "[PATH FIRST DIRFD FAIL] unexpected dynamic interpreter $1" >&2
  exit 1
fi
if [ "$1" = amd64 ];then
  (ulimit -c 0;timeout 30s "$out") >build/source-busybox/amd64/path-first-dirfd-linux-oracle.log || {
    cat build/source-busybox/amd64/path-first-dirfd-linux-oracle.log >&2
    exit 1
  }
  grep -Fxq PATH_FIRST_DIRFD_NATIVE_PASS \
    build/source-busybox/amd64/path-first-dirfd-linux-oracle.log
  echo '[PATH FIRST DIRFD LINUX ORACLE PASS] exact static x86 ELF'
fi
echo "[PATH FIRST DIRFD TARGET BUILD PASS] $1 static ELF"
