#!/bin/sh
set -eu
mkdir -p build/m100-signal
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
   -ffunction-sections -fdata-sections -Wl,--gc-sections -I kernel \
   scripts/test-m100-signal-failure.c kernel/task/signal.c \
   -o build/m100-signal/host
build/m100-signal/host
for arch in riscv64 aarch64; do
  case "$arch" in
    riscv64) flags='--target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mno-relax -mcmodel=medany' ;;
    aarch64) flags='--target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only' ;;
  esac
  clang $flags -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
     -fno-builtin -fno-stack-protector -fno-pic -fno-pie -I kernel \
     -c kernel/task/signal.c -o "build/m100-signal/$arch.o"
  echo "[M100 TARGET PASS] $arch fatal signal isolation"
done
