#!/bin/sh
# Exercise real compiled VFS dispatch, symlink walk and inode I/O contracts.
set -eu
mkdir -p build/vfs-provider
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
  scripts/test-vfs-provider.c kernel/fs/vfs.c kernel/fs/vfs_providers.c \
  kernel/fs/lookup.c kernel/lib/string.c -o build/vfs-provider/test
build/vfs-provider/test
if command -v clang >/dev/null 2>&1; then
  for target in x86_64-unknown-none-elf riscv64-unknown-elf aarch64-none-elf; do
    for source in kernel/fs/vfs.c kernel/fs/vfs_providers.c kernel/fs/lookup.c; do
      clang --target="$target" -std=c11 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -I kernel -fsyntax-only "$source"
    done
  done
fi
