#!/bin/sh
set -eu
mkdir -p build/h1-pci-ecam
cc -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
    -I kernel tests/h1-pci-ecam-host.c \
    kernel/platform/pci_ecam.c kernel/drivers/core/pci_scan.c \
    -o build/h1-pci-ecam/host-test
build/h1-pci-ecam/host-test
if command -v clang >/dev/null 2>&1; then
    for target in x86_64-unknown-none-elf riscv64-unknown-elf aarch64-none-elf; do
        clang --target="$target" -std=c11 -Wall -Wextra -Werror \
            -ffreestanding -fno-builtin -I kernel -fsyntax-only \
            kernel/platform/pci_ecam.c
    done
fi
