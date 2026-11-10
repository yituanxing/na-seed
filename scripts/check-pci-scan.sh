#!/bin/sh
# The hardware-discovery parser has no config write callback or driver side
# effects. Test synthetic PCI bridges/NVMe/multifunction before real firmware.
set -eu
mkdir -p build/pci-scan
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
    scripts/test-pci-scan.c kernel/drivers/core/pci_scan.c \
    -o build/pci-scan/test
build/pci-scan/test
# Verify actual freestanding x86 object generation, including port-I/O asm.
cc -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
    -mno-red-zone -I kernel -c kernel/platform/x86_h0_pci.c \
    -o build/pci-scan/x86_h0_pci.o
if command -v clang >/dev/null 2>&1; then
    for target in x86_64-unknown-none-elf riscv64-unknown-elf aarch64-none-elf; do
        clang --target="$target" -std=c11 -Wall -Wextra -Werror \
            -ffreestanding -fno-builtin -I kernel -fsyntax-only \
            kernel/drivers/core/pci_scan.c
    done
    clang --target=x86_64-unknown-none-elf -std=c11 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -I kernel -fsyntax-only \
        kernel/platform/x86_h0_pci.c
fi
