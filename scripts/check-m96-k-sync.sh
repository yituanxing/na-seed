#!/bin/sh
set -eu
mkdir -p build/m96-k-sync
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
    scripts/test-m96-k-sync.c kernel/task/k_sync.c kernel/task/kthread.c \
    kernel/task/wait_queue.c kernel/task/kernel_context.c \
    kernel/arch/x86_64/kernel_switch.S -o build/m96-k-sync/host
build/m96-k-sync/host
for arch in riscv64 aarch64; do
    case "$arch" in
      riscv64) flags='--target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mno-relax -mcmodel=medany' ;;
      aarch64) flags='--target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only' ;;
    esac
    clang $flags -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
        -fno-builtin -fno-stack-protector -fno-pic -fno-pie -I kernel \
        -c kernel/task/k_sync.c -o "build/m96-k-sync/$arch.o"
    echo "[M96 TARGET PASS] $arch kernel semaphore/mailbox"
done
