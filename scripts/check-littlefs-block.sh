#!/bin/sh
set -eu
test "$(cat third_party/littlefs/UPSTREAM_REVISION)" = "adad0fbbcf5382c20978d07f94f9c13be9041c1b"
test -f third_party/littlefs/LICENSE.md
test -f third_party/littlefs/lfs.c
mkdir -p build/littlefs-block
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
  -I kernel -I third_party/littlefs \
  scripts/test-littlefs-block.c ports/littlefs/block_adapter.c \
  kernel/block/device.c kernel/block/ramdisk.c kernel/lib/string.c \
  third_party/littlefs/lfs.c third_party/littlefs/lfs_util.c \
  -o build/littlefs-block/real-littlefs
build/littlefs-block/real-littlefs
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
  -I kernel -I third_party/littlefs \
  scripts/test-littlefs-vfs.c ports/littlefs/vfs_port.c \
  ports/littlefs/block_adapter.c kernel/fs/vfs.c kernel/fs/lookup.c \
  kernel/block/device.c kernel/block/ramdisk.c kernel/lib/string.c \
  third_party/littlefs/lfs.c third_party/littlefs/lfs_util.c \
  -o build/littlefs-block/real-vfs
build/littlefs-block/real-vfs
# Compile the bridge for every supported ISA, but do NOT claim that
# source built against hosted libc has passed a guest kernel integration.
if command -v clang >/dev/null 2>&1; then
  for target in x86_64-unknown-none-elf riscv64-unknown-elf aarch64-none-elf; do
    for source in ports/littlefs/block_adapter.c ports/littlefs/vfs_port.c third_party/littlefs/lfs.c third_party/littlefs/lfs_util.c kernel/lib/string.c; do
      clang --target="$target" -std=c11 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -DLFS_NO_MALLOC -DLFS_NO_DEBUG \
        -DLFS_NO_WARN -DLFS_NO_ERROR -DLFS_NO_ASSERT \
        -I ports/littlefs/freestanding/include -I kernel \
        -I third_party/littlefs -fsyntax-only "$source"
    done
  done
fi
