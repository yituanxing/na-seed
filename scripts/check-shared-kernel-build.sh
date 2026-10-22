#!/bin/sh
# Whole-tree shared kernel compilation gate for RISC-V64 and AArch64.
# Compile *every* generic C translation unit with the actual target ISA;
# do not hide syntax failures behind NASEED_PORT_M0, and never invent a
# successful full-kernel link before context/MMU/syscall providers exist.
set -eu
: "${ARCH:?set ARCH=riscv64 or aarch64}"
case "$ARCH" in
  riscv64) flags="--target=riscv64-unknown-elf -march=rv64imac -mabi=lp64 -mcmodel=medany -mno-relax" ;;
  aarch64) flags="--target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only" ;;
  *) echo "ARCH=$ARCH unsupported" >&2; exit 2 ;;
esac
test -s build/deps/limine.h
count=0
for f in $(find kernel -name '*.c' -type f | LC_ALL=C sort); do
    case "$f" in
      kernel/arch/*|kernel/platform/*) continue ;;
    esac
    clang $flags -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
      -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
      -I build/deps -I kernel -fsyntax-only "$f"
    count=$((count+1))
done
test "$count" -ge 19
echo "[PORT CONTRACT] $ARCH generic TUs syntax checked=$count"
