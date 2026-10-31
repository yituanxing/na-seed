#!/bin/sh
# Host exercises the REAL common block code; cross-target syntax gates ensure
# device implementations cannot drift between x86_64, riscv64 and aarch64.
set -eu
mkdir -p build/block-foundation
# Official BSD lwIP source is a pinned external dependency, not copied
# NaOS GPL code. A fresh checkout need not populate the gitlink until the
# lwIP adapter is compiled in the next integration batch.
test "$(git ls-files --stage third_party/lwip | awk '{print $1 " " $2}')" = \
  "160000 77dcd25a72509eb83f72b033d219b1d40cd8eb95"
grep -Fq 'url = https://github.com/lwip-tcpip/lwip.git' .gitmodules
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
  scripts/test-block-foundation.c kernel/block/device.c kernel/block/ramdisk.c \
  kernel/lib/string.c -o build/block-foundation/test
build/block-foundation/test
# Original MBR/GPT bytes, CRC, live parent references and 512/4096 views.
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
  scripts/test-block-partition.c kernel/block/device.c \
  kernel/block/ramdisk.c kernel/block/partition.c kernel/lib/string.c \
  -o build/block-foundation/test-partition
build/block-foundation/test-partition
if command -v clang >/dev/null 2>&1; then
  for target in x86_64-unknown-none-elf riscv64-unknown-elf aarch64-none-elf; do
    for source in kernel/block/device.c kernel/block/ramdisk.c \
                  kernel/block/partition.c; do
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
  kernel/block/device.c kernel/block/partition.c kernel/lib/string.c \
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
    # Driver bindings are now verified by real QEMU and original U-Boot host test.
  done
fi

# Hardware-provider-independent bus matching, backend binding and lifetime.
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
  scripts/test-driver-bus.c kernel/drivers/core/bus.c \
  kernel/drivers/port/block.c kernel/block/device.c kernel/lib/string.c \
  -o build/block-foundation/test-driver-bus
build/block-foundation/test-driver-bus
# Ethernet framing has one driver port/registry and no duplicate TCP/IP.
# Mock Ethernet RX/TX is a host boundary test, NOT a claim of physical NIC.
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
  scripts/test-network-device-port.c kernel/drivers/port/net.c \
  kernel/lib/string.c -o build/block-foundation/test-network-device-port
build/block-foundation/test-network-device-port
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
  scripts/test-driver-dma.c kernel/drivers/port/dma.c \
  -o build/block-foundation/test-driver-dma
build/block-foundation/test-driver-dma
if command -v clang >/dev/null 2>&1; then
  for target in x86_64-unknown-none-elf riscv64-unknown-elf aarch64-none-elf; do
    for source in kernel/drivers/core/bus.c kernel/drivers/port/dma.c \
                  kernel/drivers/port/net.c; do
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


# Shared split-virtqueue adapter uses upstream Unikraft ring layout without
# importing a fake storage driver. It is a prerequisite, NOT disk acceptance.
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
  -I third_party/unikraft/include -I ports/unikraft/include \
  scripts/test-virtio-split-queue.c ports/unikraft/split_queue.c \
  kernel/drivers/port/dma.c kernel/drivers/virtio/transport.c \
  kernel/lib/string.c -o build/block-foundation/test-virtio-split-queue
build/block-foundation/test-virtio-split-queue
if command -v clang >/dev/null 2>&1; then
  for target in x86_64-unknown-none-elf riscv64-unknown-elf aarch64-none-elf; do
    clang --target="$target" -std=c11 -Wall -Wextra -Werror \
      -ffreestanding -fno-builtin -I kernel \
      -I third_party/unikraft/include -I ports/unikraft/include \
      -fsyntax-only ports/unikraft/split_queue.c
  done
fi

# U-Boot *original* GPL-2.0+ VirtIO-Blk source, byte-identical to pinned upstream:
test "$(git hash-object third_party/uboot/drivers/virtio/virtio_blk.c)" = "7b1d891cdcbb5d5795ee64f0094ad4c64bb12c52"
test "$(git hash-object third_party/uboot/drivers/virtio/virtio_blk.h)" = "b37ba264df4b6ab6d7ed4c2cbfe580a58710799e"
# ORIGINAL U-Boot VirtIO-Net from the SAME pinned revision as VirtIO-Blk.
# Compile its native source/ABI before linking a live NIC in a later batch.
test "$(git hash-object third_party/uboot/drivers/virtio/virtio_net.c)" = "71e4d2513964c7484114642453dffad69b4b5df9"
test "$(git hash-object third_party/uboot/drivers/virtio/virtio_net.h)" = "3adcb19aead299d3fa28c747a292ee39ef68f6f8"
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter \
  -Wno-sign-compare -fno-builtin -I ports/uboot/compat -I kernel \
  -I third_party/unikraft/include -fsyntax-only \
  third_party/uboot/drivers/virtio/virtio_net.c
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
  -I ports/uboot/compat -I kernel \
  scripts/test-uboot-virtio-net-abi.c \
  -o build/block-foundation/test-uboot-virtio-net-abi
build/block-foundation/test-uboot-virtio-net-abi
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
  -I ports/uboot/compat -I kernel -I third_party/unikraft/include \
  -I ports/unikraft/include \
  scripts/test-uboot-original-virtio-blk-port.c ports/uboot/blk_port.c \
  ports/unikraft/split_queue.c kernel/drivers/virtio/transport.c \
  kernel/drivers/port/dma.c kernel/drivers/port/block.c \
  kernel/block/device.c kernel/lib/string.c \
  -o build/block-foundation/test-uboot-original-port
build/block-foundation/test-uboot-original-port
if command -v clang >/dev/null 2>&1; then
  for target in x86_64-unknown-none-elf riscv64-unknown-elf aarch64-none-elf; do
    clang --target="$target" -std=c11 -Wall -Wextra -Werror \
      -ffreestanding -fno-builtin -I ports/uboot/compat -I kernel \
      -I third_party/unikraft/include -I ports/unikraft/include \
      -fsyntax-only ports/uboot/blk_port.c ports/uboot/virtio_bus.c
    clang --target="$target" -std=c11 -Wall -Wextra -Werror \
      -Wno-unused-parameter -Wno-sign-compare \
      -ffreestanding -fno-builtin -I ports/uboot/compat -I kernel \
      -I third_party/unikraft/include -fsyntax-only \
      third_party/uboot/drivers/virtio/virtio_net.c
  done
fi

# Modern PCI transport is a real independent backend behind the SAME
# nas_virtio_transport_ops; host checks capability bounds and queue MMIO.
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
  scripts/test-virtio-pci.c kernel/drivers/virtio/pci.c \
  -o build/block-foundation/test-virtio-pci
build/block-foundation/test-virtio-pci
if command -v clang >/dev/null 2>&1; then
  for target in x86_64-unknown-none-elf riscv64-unknown-elf aarch64-none-elf; do
    clang --target="$target" -std=c11 -Wall -Wextra -Werror \
      -ffreestanding -fno-builtin -I kernel -fsyntax-only \
      kernel/drivers/virtio/pci.c
  done
fi
