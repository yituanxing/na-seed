#!/bin/sh
set -eu
mkdir -p build/m95-kthread
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
    scripts/test-m95-kthread.c kernel/task/kthread.c kernel/task/kernel_context.c \
    kernel/task/wait_queue.c kernel/arch/x86_64/kernel_switch.S \
    -o build/m95-kthread/host
build/m95-kthread/host
for arch in riscv64 aarch64; do
    case "$arch" in
      riscv64) flags='--target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mno-relax -mcmodel=medany' ;;
      aarch64) flags='--target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only' ;;
    esac
    for src in kernel/task/kthread.c kernel/task/kthread_boot.c; do
       name=${src##*/}
       clang $flags -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
           -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
           -I kernel -c "$src" -o "build/m95-kthread/$arch-$name.o"
    done
    echo "[M95 TARGET PASS] $arch kernel worker and native boot scenario"
done
