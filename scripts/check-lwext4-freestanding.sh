#!/bin/sh
# Compile the ACTUAL vendored lwext4 source against the permanent NaSeed
# freestanding ABI on every supported ISA. Never patch the vendor sources.
set -eu
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
  scripts/test-kheap.c kernel/mm/heap.c kernel/lib/string.c \
  -o /tmp/naseed-kheap-test
/tmp/naseed-kheap-test
vendor=third_party/lwext4
port=ports/lwext4/freestanding
for triple in x86_64-unknown-none-elf riscv64-unknown-elf aarch64-none-elf; do
  for c in "$vendor"/src/*.c; do
    clang --target="$triple" -std=c11 -ffreestanding -fno-builtin \
      -Werror=implicit-function-declaration -Werror=incompatible-pointer-types \
      -I "$port/include" -I "$vendor/include" -I kernel \
      -include "$port/platform.h" -fsyntax-only "$c"
  done
  clang --target="$triple" -std=c11 -ffreestanding -fno-builtin \
    -Wall -Wextra -Werror -I kernel -fsyntax-only kernel/mm/heap.c
  clang --target="$triple" -std=c11 -ffreestanding -fno-builtin \
    -Wall -Wextra -Werror -I "$port/include" -I kernel \
    -fsyntax-only "$port/compat.c"
  echo "[LWEXT4 FREESTANDING] $triple library + allocator + C runtime PASS"
done
