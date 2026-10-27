#!/bin/sh
# One common userspace source + real ISA/ABI wrappers. HOST cross-build only.
set -eu
out=build/lwext4-native-rw
for port in x86_64 riscv64 aarch64; do
    dir="$out/$port"
    mkdir -p "$dir"
    case "$port" in
        x86_64)
            flags="--target=x86_64-unknown-none-elf -mno-red-zone"
            emulation=elf_x86_64
            linker=user/ext4-guest-rw-x86.ld ;;
        riscv64)
            flags="--target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mcmodel=medany -mno-relax"
            emulation=elf64lriscv
            linker=user/riscv64/linker.ld ;;
        aarch64)
            flags="--target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only"
            emulation=aarch64elf
            linker=user/aarch64/linker.ld ;;
    esac
    clang $flags -O2 -std=c11 -Wall -Wextra -Werror -ffreestanding \
      -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
      -c user/ext4-guest-rw.c -o "$dir/rw.o"
    clang $flags -c "user/$port/ext4-rw-syscall.S" -o "$dir/syscall.o"
    ld.lld -m "$emulation" -nostdlib -static --no-relax \
      -z max-page-size=0x1000 -T "$linker" \
      "$dir/syscall.o" "$dir/rw.o" -o "$dir/probe.elf"
    readelf -h "$dir/probe.elf" | grep -q 'Type:.*EXEC'
    test -s "$dir/probe.elf"
    echo "[LWEXT4 COMMON RW ELF PASS] $port same source / target syscall ABI"
done
