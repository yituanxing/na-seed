#!/bin/sh
# Exercise real compiled VFS dispatch, symlink walk and inode I/O contracts.
set -eu
mkdir -p build/vfs-provider
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
  scripts/test-vfs-provider.c kernel/fs/vfs.c kernel/fs/vfs_providers.c \
  kernel/fs/lookup.c kernel/lib/string.c kernel/mm/heap.c \
  -o build/vfs-provider/test
build/vfs-provider/test
# A deleted duplicate mount-inode owner is an architectural invariant:
# positive Dentry/Path handles BOTH covered and parent mount lifetimes.
# Never reintroduce covered_pinned / parent_pinned in another refactor.
if grep -Eq '\\b(parent_pinned|covered_pinned)\\b' kernel/fs/vfs.c; then
  echo '[VFS FAIL] stale parallel mount pin ownership returned' >&2
  exit 1
fi
# Mount identity exists exactly once in its owning Dentry Path, not an
# independent pointer/inode tuple that may diverge after rename/reuse.
if grep -Eq 'parent_mount_id|parent_backend_inode|parent_ino|parent_node|covered_node|parent_anchored' kernel/fs/vfs.c; then
  echo '[VFS FAIL] copied mount identity escaped its Path owner' >&2
  exit 1
fi
if command -v clang >/dev/null 2>&1; then
  for target in x86_64-unknown-none-elf riscv64-unknown-elf aarch64-none-elf; do
    for source in kernel/fs/vfs.c kernel/fs/vfs_providers.c kernel/fs/lookup.c; do
      clang --target="$target" -std=c11 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -I kernel -fsyntax-only "$source"
    done
  done
fi
