#!/bin/sh
# Host exercises the REAL common block code; cross-target syntax gates ensure
# device implementations cannot drift between x86_64, riscv64 and aarch64.
set -eu
mkdir -p build/block-foundation
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
  scripts/test-block-foundation.c kernel/block/device.c kernel/block/ramdisk.c \
  kernel/lib/string.c -o build/block-foundation/test
build/block-foundation/test
if command -v clang >/dev/null 2>&1; then
  for target in x86_64-unknown-none-elf riscv64-unknown-elf aarch64-none-elf; do
    for source in kernel/block/device.c kernel/block/ramdisk.c; do
      clang --target="$target" -std=c11 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -I kernel -fsyntax-only "$source"
    done
  done
fi

# Third-party driver ports are not another block registry: the real
# current VirtIO-MMIO driver also uses this adapter in native AArch64.
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
  scripts/test-block-driver-port.c kernel/drivers/port/block.c \
  kernel/block/device.c kernel/lib/string.c \
  -o build/block-foundation/test-driver-port
build/block-foundation/test-driver-port
if command -v clang >/dev/null 2>&1; then
  for target in x86_64-unknown-none-elf riscv64-unknown-elf aarch64-none-elf; do
    clang --target="$target" -std=c11 -Wall -Wextra -Werror \
      -ffreestanding -fno-builtin -I kernel -fsyntax-only \
      kernel/drivers/port/block.c
    clang --target="$target" -std=c11 -Wall -Wextra -Werror \
      -ffreestanding -fno-builtin -I kernel -fsyntax-only \
      kernel/drivers/block/virtio_mmio_blk.c
  done
fi
