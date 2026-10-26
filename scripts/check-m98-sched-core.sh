#!/bin/sh
set -eu
mkdir -p build/m98-sched-core
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
    scripts/test-m98-sched-core.c kernel/task/sched_core.c \
    -o build/m98-sched-core/host
build/m98-sched-core/host
for arch in riscv64 aarch64; do
    case "$arch" in
      riscv64) flags='--target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mno-relax -mcmodel=medany' ;;
      aarch64) flags='--target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only' ;;
    esac
    clang $flags -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
         -fno-builtin -fno-stack-protector -fno-pic -fno-pie -I kernel \
         -c kernel/task/sched_core.c -o "build/m98-sched-core/$arch.o"
    echo "[M98 TARGET PASS] $arch common scheduler candidate policy"
done
