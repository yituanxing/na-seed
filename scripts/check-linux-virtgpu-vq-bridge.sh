#!/bin/sh
# Prove that the REAL NaSeed split queue can batch original Linux VirtIO-GPU
# command submissions without an extra queue or synthetic completion.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
temp=$(mktemp -d)
trap 'rm -rf "$temp"' EXIT HUP INT TERM
inc="-Ithird_party/unikraft/include -Iports/unikraft/include"
for arch in x86_64 riscv64 aarch64; do
    case "$arch" in
        x86_64) target=x86_64-unknown-none-elf ;;
        riscv64) target=riscv64-unknown-elf ;;
        aarch64) target=aarch64-none-elf ;;
    esac
    for src in kernel/drivers/port/dma.c ports/unikraft/split_queue.c ports/linux-virtgpu/virtqueue_bridge.c ports/linux-virtgpu/gem_dma_port.c; do
        clang --target="$target" -std=c11 -ffreestanding -fno-builtin \
            -Wall -Wextra -Werror $inc -c "$src" \
            -o "$temp/${arch}-$(basename "$src" .c).o"
    done
    echo "PASS: real split queue + Linux virtqueue cookie bridge cross-build $arch"
done
cc -std=c11 -O2 -Wall -Wextra -Werror $inc \
    tests/linux-virtgpu-vq-bridge.c \
    ports/unikraft/split_queue.c ports/linux-virtgpu/virtqueue_bridge.c \
    -o "$temp/vq-bridge-test"
"$temp/vq-bridge-test"
echo "PASS: real split queue deferred/notify, out-of-order completion, cookie lifetime and eager fallback"
cc -std=c11 -O2 -Wall -Wextra -Werror $inc \
    tests/linux-virtgpu-dma-frames.c kernel/drivers/port/dma.c \
    ports/linux-virtgpu/gem_dma_port.c \
    -o "$temp/dma-frame-test"
"$temp/dma-frame-test"
echo "PASS: real DMA page pins, map/unmap, nonidentity IOMMU and error rollback"
cc -std=c11 -O2 -Wall -Wextra -Werror $inc \
    tests/linux-virtgpu-gem-queue-e2e.c \
    kernel/drivers/port/dma.c \
    ports/unikraft/split_queue.c \
    ports/linux-virtgpu/virtqueue_bridge.c \
    ports/linux-virtgpu/gem_dma_port.c \
    -o "$temp/gem-queue-e2e"
"$temp/gem-queue-e2e"
echo "PASS: one-shot GEM DMA pin -> mapped vring -> used completion -> safe release"
