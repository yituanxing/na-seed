#!/bin/sh
set -eu
mkdir -p build/m92-wait-queue
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
    scripts/test-m92-wait-queue.c kernel/task/wait_queue.c \
    -o build/m92-wait-queue/host
build/m92-wait-queue/host
for arch in riscv64 aarch64; do
    case "$arch" in
      riscv64) flags='--target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mno-relax -mcmodel=medany' ;;
      aarch64) flags='--target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only' ;;
    esac
    clang $flags -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
        -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
        -c kernel/task/wait_queue.c -o "build/m92-wait-queue/$arch.o"
    echo "[M92 TARGET PASS] $arch generic wait queue"
done
