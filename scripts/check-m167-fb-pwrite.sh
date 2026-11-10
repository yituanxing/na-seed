#!/bin/sh
# Real positioned FileOps and framebuffer provider, independent of QEMU.
set -eu
mkdir -p build/m167-fb-positioned
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -I kernel -I scripts/m89-test-include \
    scripts/test-m167-fb-pwrite.c kernel/fs/file_io.c \
    kernel/drivers/display/fbdev.c \
    -o build/m167-fb-positioned/test-fb-pwrite
build/m167-fb-positioned/test-fb-pwrite
if command -v clang >/dev/null 2>&1; then
    for target in x86_64-unknown-none-elf riscv64-unknown-elf aarch64-none-elf; do
        clang --target="$target" -std=c11 -Wall -Wextra -Werror \
            -ffreestanding -fno-builtin \
            -I kernel -I scripts/m89-test-include \
            -I third_party/unikraft/include -I ports/unikraft/include \
            -I ports/lwip/include -I third_party/lwip/src/include \
            -fsyntax-only kernel/fs/file_io.c kernel/fs/syscall.c \
            kernel/drivers/display/fbdev.c
    done
fi
