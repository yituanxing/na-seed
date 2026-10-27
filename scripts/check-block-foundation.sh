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

# Verify the pinned upstream source is byte-identical to the imported blobs.
test "$(git hash-object third_party/unikraft/include/virtio/virtio_ring.h)" = "9909bcc1f175d7bead456005adf57d4c75d7a2f3"
test "$(git hash-object third_party/unikraft/include/virtio/virtio_types.h)" = "f71be56b7ad839e0ef40d31963882351ee584f78"
test "$(git hash-object third_party/unikraft/include/virtio/virtio_blk.h)" = "7f09b3d55b2cc492105beaf73dc99f08b20f9d4f"
test "$(git hash-object third_party/unikraft/include/virtio/virtio_ids.h)" = "7fa6166980bca4a61ec666cef40f4fce4112b512"
test "$(git hash-object third_party/unikraft/drivers/virtio_blk.c)" = "81eb5e284f418c78f5023b17142fc679114f3908"
cc -std=c11 -O2 -Wall -Wextra -Werror \
  -I third_party/unikraft/include -I ports/unikraft/include \
  scripts/test-unikraft-ring.c -o build/block-foundation/test-unikraft-ring
cc -std=c11 -O2 -Wall -Wextra -Werror \
  -I third_party/unikraft/include -I ports/unikraft/include \
  scripts/test-unikraft-blk-abi.c -o build/block-foundation/test-unikraft-blk-abi
build/block-foundation/test-unikraft-blk-abi
build/block-foundation/test-unikraft-ring

# Third-party driver ports are not another block registry: the real
# the actual upstream driver remains to be integrated.
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
    for src in kernel/drivers/virtio/transport.c \
               kernel/drivers/virtio/mmio.c; do
      clang --target="$target" -std=c11 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -I kernel \
        -I third_party/unikraft/include -I ports/unikraft/include \
        -fsyntax-only "$src"
    done
    # Shared transport is verified; this is NOT a live upstream driver port.
  done
fi

# Hardware-provider-independent bus matching, backend binding and lifetime.
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
  scripts/test-driver-bus.c kernel/drivers/core/bus.c \
  kernel/drivers/port/block.c kernel/block/device.c kernel/lib/string.c \
  -o build/block-foundation/test-driver-bus
build/block-foundation/test-driver-bus
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
  scripts/test-driver-dma.c kernel/drivers/port/dma.c \
  -o build/block-foundation/test-driver-dma
build/block-foundation/test-driver-dma
if command -v clang >/dev/null 2>&1; then
  for target in x86_64-unknown-none-elf riscv64-unknown-elf aarch64-none-elf; do
    for source in kernel/drivers/core/bus.c kernel/drivers/port/dma.c; do
      clang --target="$target" -std=c11 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -I kernel -fsyntax-only "$source"
    done
  done
fi

# Transport API unit tests do not claim a block driver is installed.
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
  scripts/test-virtio-transport.c kernel/drivers/virtio/transport.c \
  kernel/drivers/virtio/mmio.c -o build/block-foundation/test-virtio-transport
build/block-foundation/test-virtio-transport

