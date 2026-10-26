#!/bin/sh
set -eu
mkdir -p build/m94-runstate
cc -std=c11 -O2 -Wall -Wextra -Werror -I kernel \
    scripts/test-m94-run-state.c kernel/task/run_state.c \
    -o build/m94-runstate/host
build/m94-runstate/host
for arch in riscv64 aarch64; do
    case "$arch" in
      riscv64) flags='--target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mno-relax -mcmodel=medany' ;;
      aarch64) flags='--target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only' ;;
    esac
    clang $flags -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
        -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
        -c kernel/task/run_state.c -o "build/m94-runstate/$arch.o"
    echo "[M94 TARGET PASS] $arch generic wake state"
done
