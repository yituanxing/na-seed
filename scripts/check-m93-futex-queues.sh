#!/bin/sh
set -eu
mkdir -p build/m93-futex
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
    scripts/test-m93-futex-queues.c kernel/task/futex_wait.c kernel/task/wait_queue.c kernel/task/run_state.c \
    -o build/m93-futex/host
build/m93-futex/host
for arch in riscv64 aarch64; do
    case "$arch" in
      riscv64) flags='--target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mno-relax -mcmodel=medany' ;;
      aarch64) flags='--target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only' ;;
    esac
    clang $flags -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
        -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
        -c kernel/task/futex_wait.c -o "build/m93-futex/$arch.o"
    echo "[M93 TARGET PASS] $arch"
done
