#!/bin/sh
# Real shared NaSeed VM policy over simulated arch PTE/physical frames.
# No mock replacement algorithm; exact vm.c executes in host preflight.
set -eu
mkdir -p build/m86-txn
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
    -ffunction-sections -fdata-sections -Wl,--gc-sections -I kernel \
    scripts/test-m86-txn.c kernel/mm/vm.c kernel/mm/anon_regions.c \
    -o build/m86-txn/check
build/m86-txn/check
