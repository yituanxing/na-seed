#!/bin/sh
# Link the real VM/invalidation, MM fork/COW, fault, VMA owner, usercopy and
# ptrace implementations; mock only target-specific page tables and file I/O.
set -eu
a005_work=$(mktemp -d "${TMPDIR:-/tmp}/naseed-a005-cross-mm-XXXXXX")
trap 'rm -rf -- "$a005_work"' EXIT HUP INT TERM
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
    -ffunction-sections -fdata-sections -Wl,--gc-sections -I kernel \
    scripts/test-a005-cross-mm.c kernel/mm/vm.c kernel/mm/mm.c \
    kernel/mm/fault.c kernel/mm/usercopy.c kernel/mm/remote_copy.c \
    kernel/mm/anon_regions.c kernel/mm/heap.c \
    -o "$a005_work/test"
"$a005_work/test"
