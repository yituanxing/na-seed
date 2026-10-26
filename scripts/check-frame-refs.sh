#!/bin/sh
# Real frame.c linked to a synthetic Limine map; no downloads or mock page logic.
set -eu
mkdir -p build/frame-owner-test
cat > build/frame-owner-test/limine.h <<'EOF'
#ifndef NAS_TEST_LIMINE_H
#define NAS_TEST_LIMINE_H
#include <stdint.h>
#define LIMINE_MEMMAP_USABLE 0
struct limine_memmap_entry {uint64_t base, length, type;};
struct limine_memmap_response {
    uint64_t revision, entry_count;
    struct limine_memmap_entry **entries;
};
#endif
EOF
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
  -I build/frame-owner-test scripts/test-frame-refs.c kernel/mm/frame.c \
  -o build/frame-owner-test/check
build/frame-owner-test/check
