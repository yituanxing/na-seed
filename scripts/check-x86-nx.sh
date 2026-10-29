#!/bin/sh
# The production x86 MMU is linked unmodified. Only privileged CPU state
# and physical frame provisioning are host-simulated, not the PTE logic.
set -eu
mkdir -p build/x86-nx
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
    -ffunction-sections -fdata-sections -Wl,--gc-sections -I kernel \
    scripts/test-x86-nx.c kernel/arch/x86_64/mmu.c \
    -o build/x86-nx/check
build/x86-nx/check
