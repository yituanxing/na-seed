#!/bin/sh
set -eu
mkdir -p build/m102-tty-cow
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
    -ffunction-sections -fdata-sections -Wl,--gc-sections -I scripts/m89-test-include -I kernel \
    scripts/test-m102-tty-cow.c kernel/fs/file_io.c kernel/drivers/tty.c \
    kernel/drivers/tty_ldisc.c -o build/m102-tty-cow/host
build/m102-tty-cow/host
for arch in riscv64 aarch64; do
    case "$arch" in
      riscv64) flags='--target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mno-relax -mcmodel=medany' ;;
      aarch64) flags='--target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only' ;;
    esac
    clang $flags -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
        -fno-builtin -fno-stack-protector -fno-pic -fno-pie -I kernel \
        -c kernel/drivers/tty.c -o "build/m102-tty-cow/$arch.o"
    clang $flags -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
        -fno-builtin -fno-stack-protector -fno-pic -fno-pie -I kernel \
        -c kernel/fs/file_io.c -o "build/m102-tty-cow/$arch-file-io.o"
    echo "[M102 TARGET PASS] $arch TTY + shared FileOps usercopy/COW adapter"
done
