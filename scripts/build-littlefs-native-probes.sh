#!/bin/sh
# Identical Linux ELF filesystem test source, existing proven syscall ABI.
set -eu
for arch in x86_64 riscv64 aarch64; do
  out=build/littlefs-native/$arch
  mkdir -p "$out"
  case "$arch" in
    x86_64) flags="--target=x86_64-unknown-none-elf -mno-red-zone"
            emulation=elf_x86_64; linker=user/ext4-guest-rw-x86.ld ;;
    riscv64) flags="--target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mcmodel=medany -mno-relax"
             emulation=elf64lriscv; linker=user/riscv64/linker.ld ;;
    aarch64) flags="--target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only"
             emulation=aarch64elf; linker=user/aarch64/linker.ld ;;
  esac
  clang $flags -O2 -std=c11 -Wall -Wextra -Werror -ffreestanding \
    -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
    -c user/littlefs-native-vfs.c -o "$out/program.o"
  clang $flags -c "user/$arch/ext4-rw-syscall.S" -o "$out/syscall.o"
  ld.lld -m "$emulation" -nostdlib -static --no-relax \
    -z max-page-size=0x1000 -T "$linker" \
    "$out/program.o" "$out/syscall.o" -o "$out/probe.elf"
  readelf -h "$out/probe.elf" | grep -q 'Type:.*EXEC'
  echo "[LITTLEFS PORTABLE ELF PASS] $arch same freestanding source"
done
