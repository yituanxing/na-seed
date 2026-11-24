#!/bin/sh
set -eu
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cc -std=c11 -O2 -Wall -Wextra -Werror \
  scripts/test-pt-directory.c kernel/arch/aarch64/mmu.c \
  kernel/arch/riscv64/mmu.c kernel/arch/aarch64/mmu_test.c \
  kernel/arch/riscv64/mmu_test.c -o "$tmp/pt"
"$tmp/pt"
