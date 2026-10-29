#!/bin/sh
# M105 host proofs: link the production PT_LOAD engine + actual MM/COW owner.
# Does not claim guest QEMU or hardware-I-cache execution coverage.
set -eu
m105_tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/naseed-m105-XXXXXX")
trap 'rm -rf -- "$m105_tmp_dir"' EXIT HUP INT TERM
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
    scripts/test-m105-elf-permissions.c kernel/task/elf_segments.c \
    kernel/mm/anon_regions.c \
    -o "$m105_tmp_dir/elf-permissions"
"$m105_tmp_dir/elf-permissions"
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
    -ffunction-sections -fdata-sections -Wl,--gc-sections -I kernel \
    scripts/test-m105-elf-cow.c kernel/mm/mm.c kernel/mm/fault.c \
    kernel/mm/usercopy.c kernel/mm/remote_copy.c kernel/mm/anon_regions.c \
    -o "$m105_tmp_dir/elf-cow"
"$m105_tmp_dir/elf-cow"
echo '[M105 HOST PASS] real ELF parser/mapping callbacks and MM COW owner, mock hardware page table'
