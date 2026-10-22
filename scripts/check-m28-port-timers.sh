#!/bin/sh
# Architecture timer providers must compile with exactly the native C
# flags and native assembly ISA used by the full QEMU kernel builders.
# This catches IRQ-frame and CSR mistakes BEFORE launching firmware.
set -eu
mkdir -p build/m28-ports
for arch in riscv64 aarch64; do
    case "$arch" in
        riscv64)
            target=--target=riscv64-unknown-elf
            flags='-march=rv64gc -mabi=lp64 -mcmodel=medany -mno-relax'
            ;;
        aarch64)
            target=--target=aarch64-none-elf
            flags='-march=armv8-a -mgeneral-regs-only'
            ;;
    esac
    for src in kernel/arch/"$arch"/cpu.c \
               kernel/arch/"$arch"/context.c \
               kernel/arch/"$arch"/timer.c; do
        dst="build/m28-ports/$arch-$(basename "$src" .c).o"
        clang "$target" $flags -std=c11 -O2 -Wall -Wextra -Werror \
          -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
          -I kernel -c "$src" -o "$dst"
    done
    if [ "$arch" = aarch64 ]; then
        clang "$target" -march=armv8-a -c kernel/arch/aarch64/trap.S \
          -o build/m28-ports/aarch64-trap.o
        clang "$target" $flags -std=c11 -O2 -Wall -Wextra -Werror \
          -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
          -I kernel -c kernel/arch/aarch64/debug_console.c \
          -o build/m28-ports/aarch64-console.o
    else
        clang "$target" $flags -c kernel/arch/riscv64/trap.S \
          -o build/m28-ports/riscv64-trap.o
    fi
    echo "[M28 PORT PRELIGHT PASS] $arch IRQ trap, timer and context objects"
done
