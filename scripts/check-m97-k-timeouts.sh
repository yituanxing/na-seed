#!/bin/sh
set -eu
mkdir -p build/m97-k-timeouts
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
    scripts/test-m97-k-timeouts.c kernel/task/k_sync.c kernel/task/kthread.c \
    kernel/task/wait_queue.c kernel/task/kernel_context.c \
    kernel/arch/x86_64/kernel_switch.S -o build/m97-k-timeouts/host
build/m97-k-timeouts/host
for arch in riscv64 aarch64; do
    case "$arch" in
      riscv64) flags='--target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mno-relax -mcmodel=medany' ;;
      aarch64) flags='--target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only' ;;
    esac
    for src in kernel/task/kthread.c kernel/task/k_sync.c kernel/task/scheduler_service.c; do
       base=${src##*/}
       clang $flags -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
           -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
           -I kernel -c "$src" -o "build/m97-k-timeouts/$arch-$base.o"
    done
    echo "[M97 TARGET PASS] $arch timed kernel continuations"
done
