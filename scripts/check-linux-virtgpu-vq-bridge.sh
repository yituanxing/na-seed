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
    for src in ports/unikraft/split_queue.c ports/linux-virtgpu/virtqueue_bridge.c; do
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
