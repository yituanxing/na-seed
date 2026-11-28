#!/bin/sh
# Compile canonical Linux v6.14 VirtIO GPU + DRM ioctl structs, on all NaSeed
# architecture targets. This is a wire-ABI compile gate, NOT a GPU driver test.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
temp=$(mktemp -d)
trap 'rm -rf "$temp"' EXIT HUP INT TERM
for arch in x86_64 riscv64 aarch64; do
    clang --target="$arch-unknown-none-elf" \
        -std=c11 -ffreestanding -fno-builtin -Wall -Wextra -Werror \
        -Iports/linux-virtgpu/include \
        -Ithird_party/linux-virtgpu/include/uapi \
        -Ithird_party/linux-virtgpu/include/uapi/drm \
        -c tests/linux-virtgpu-v6.14-abi.c -o "$temp/$arch.o"
    test -s "$temp/$arch.o"
    echo "PASS: original Linux v6.14 VirtIO-GPU DRM UAPI cross-compile $arch"
done
