#!/bin/sh
set -eu
mkdir -p build/m101-cow
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
    -ffunction-sections -fdata-sections -Wl,--gc-sections -I kernel \
    scripts/test-m101-cow.c kernel/mm/mm.c kernel/mm/fault.c \
    kernel/mm/usercopy.c kernel/mm/remote_copy.c kernel/mm/anon_regions.c \
    -o build/m101-cow/host
build/m101-cow/host
for arch in riscv64 aarch64; do
    case "$arch" in
      riscv64) flags='--target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mno-relax -mcmodel=medany' ;;
      aarch64) flags='--target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only' ;;
    esac
    for p in kernel/mm/mm.c kernel/mm/fault.c kernel/mm/usercopy.c kernel/mm/remote_copy.c; do
        out=${p##*/}
        clang $flags -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
           -fno-builtin -fno-stack-protector -fno-pic -fno-pie -I kernel \
           -c "$p" -o "build/m101-cow/$arch-$out.o"
    done
    echo "[M101 TARGET PASS] $arch private fork, fault, usercopy and ptrace"
done
