#!/bin/sh
# Host semantic regression + target-object inspection; no network or QEMU.
set -eu
mkdir -p build/klib
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
    tests/test-klib-string.c kernel/lib/string.c -o build/klib/test-klib-string
build/klib/test-klib-string
for target in x86_64 riscv64 aarch64; do
    case "$target" in
        x86_64) flags='--target=x86_64-unknown-none-elf -mcmodel=kernel -mno-red-zone -mno-sse -mno-mmx' ;;
        riscv64) flags='--target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mcmodel=medany -mno-relax' ;;
        aarch64) flags='--target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only' ;;
    esac
    clang $flags -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
        -fno-stack-protector -fno-pic -fno-pie -c kernel/lib/string.c \
        -o "build/klib/$target.o"
    # Primitive implementations must not recursively import libc symbols.
    if [ -n "$(nm -u "build/klib/$target.o")" ]; then
        echo "[KLIB FAIL] $target primitive recurses through external libc" >&2
        exit 1
    fi
    echo "[KLIB TARGET PASS] $target freestanding object"
done
