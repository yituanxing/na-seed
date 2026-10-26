#!/bin/sh
set -eu
mkdir -p build/m91-kcontext
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-omit-frame-pointer -fno-builtin \
    -I kernel scripts/test-m91-kcontext.c kernel/task/kernel_context.c \
    kernel/arch/x86_64/kernel_switch.S -o build/m91-kcontext/host
build/m91-kcontext/host
for arch in riscv64 aarch64; do
    case "$arch" in
      riscv64) flags='--target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mno-relax -mcmodel=medany' ;;
      aarch64) flags='--target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only' ;;
    esac
    clang $flags -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
        -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
        -c kernel/task/kernel_context.c -o "build/m91-kcontext/$arch-context.o"
    clang $flags -c "kernel/arch/$arch/kernel_switch.S" \
        -o "build/m91-kcontext/$arch-swap.o"
    nm "build/m91-kcontext/$arch-swap.o" | grep -q 'nas_arch_kernel_swap'
    echo "[M91 TARGET PASS] $arch native resumable kernel assembly"
done
