#!/bin/sh
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in
 amd64) target=x86_64-linux-musl; machine='Advanced Micro Devices X86-64' ;;
 riscv64) target=riscv64-linux-musl; machine='RISC-V' ;;
 arm64) target=aarch64-linux-musl; machine='AArch64' ;;
 *) exit 2 ;;
esac
# Native Linux freestanding executable with real C and real DWARF.
# Compiler/linker run on GitHub host, NEVER in NaSeed.
mkdir -p build
clang --target="$target" -std=c11 -O0 -g -gdwarf-4 \
  -fno-stack-protector -fno-omit-frame-pointer -fno-pic \
  -nostdlib -static -fuse-ld=lld -Wl,--no-dynamic-linker \
  -Wl,-e,_start -o build/m76-inferior tests/m76-inferior.c
readelf -h build/m76-inferior | grep -Fq "$machine"
readelf -WS build/m76-inferior | grep -Fq '.debug_info'
readelf -Ws build/m76-inferior | grep -Fq m76_compute
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m76-gdb \
  rootfs/alpine/packages-m76-gdb.txt \
  scripts/m76-original-elfs.txt scripts/m76-rootfs-copies.tsv
echo "[M76 HOST STAGED] original GNU GDB and real $port C DWARF inferior"
