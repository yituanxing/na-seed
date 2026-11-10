#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
out=$(mktemp)
trap 'rm -f "$out"' EXIT HUP INT TERM
"${HOSTCC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror \
  -ffreestanding -fno-builtin -fno-stack-protector \
  -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -mno-red-zone -mno-sse -mno-mmx \
  -I build/deps -I kernel -I third_party/unikraft/include \
  -I ports/unikraft/include \
  tests/h7-x86-mmio-unmap-host.c kernel/arch/x86_64/mmu.c \
  -o "$out"
"$out"
