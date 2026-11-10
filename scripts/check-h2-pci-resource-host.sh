#!/bin/sh
set -eu
mkdir -p build/h2-pci-resource
cc -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
    -I kernel tests/h2-pci-resource-host.c \
    kernel/drivers/core/pci_resources.c \
    -o build/h2-pci-resource/host-test
build/h2-pci-resource/host-test
if command -v clang >/dev/null 2>&1; then
    for target in x86_64-unknown-none-elf riscv64-unknown-elf aarch64-none-elf; do
        clang --target="$target" -std=c11 -Wall -Wextra -Werror \
            -ffreestanding -fno-builtin -I kernel -fsyntax-only \
            kernel/drivers/core/pci_resources.c
    done
fi
