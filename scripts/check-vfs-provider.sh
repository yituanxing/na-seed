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
if grep -Eq '(^|[^[:alnum:]_])(parent_pinned|covered_pinned)([^[:alnum:]_]|$)' kernel/fs/vfs.c; then
  echo '[VFS FAIL] stale parallel mount pin ownership returned' >&2
  exit 1
fi
# Mount identity exists exactly once in its owning Dentry Path, not an
# independent pointer/inode tuple that may diverge after rename/reuse.
# Local variables named parent_node/covered_node are legitimate borrowed
# results of Path->inode conversion. Only persistent duplicate mount-owner
# fields are forbidden; otherwise a Path-first migration false-fails here.
if grep -Eq '(^|[^[:alnum:]_])(parent_mount_id|parent_backend_inode|parent_ino|parent_anchored)([^[:alnum:]_]|$)' kernel/fs/vfs.c; then
  echo '[VFS FAIL] copied mount identity escaped its Path owner' >&2
  exit 1
fi
# One namei is the ONLY root/symlink/dot/mount path walker. Legacy
# prefix dispatch and per-provider reverse-directory callbacks must NOT
# reappear after this migration (use owned File/FS Dentry Paths).
if grep -Eq 'mount_parent_visible|uint32_t seen=0|for\(unsigned attempt=0' kernel/fs/vfs.c; then
  echo '[VFS FAIL] duplicate direct longest-prefix resolver returned' >&2
  exit 1
fi
if grep -Eq 'nas_vfs_directory_path|\.directory_path=' \
    kernel/fs/vfs.c kernel/fs/vfs.h kernel/fs/vfs_providers.c \
    kernel/fs/namespace_syscall.c ports/lwext4/guest_vfs.c \
    ports/ramfs/vfs_port.c; then
  echo '[VFS FAIL] obsolete provider inode->path syscall dependency returned' >&2
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

# The mount registry must be pointer-stable and actually grow beyond the
# original 16 slots. Its previous fixed array silently stopped accepting
# new independent filesystems; a reallocating mount_slot array would also
# invalidate open File/Path references.
if grep -Eq 'struct mount_slot mounts\[|NAS_VFS_MOUNT_LIMIT' kernel/fs/vfs.c; then
  echo '[VFS FAIL] fixed-size/relocatable mount slot table returned' >&2
  exit 1
fi

# Hash indexing is an acceleration of the SAME positive Dentry ownership,
# not a second namespace resolver. Reject an unindexed all-dentry scan and
# a recursive last-owner destructor (real 16 KiB kernel stacks are bounded).
if ! grep -q 'dentry_bucket(parent,name,node->mount_id)' kernel/fs/vfs.c ||
   ! grep -q 'dentry_index_insert(d);' kernel/fs/vfs.c; then
  echo '[VFS FAIL] positive Dentry index bypassed' >&2
  exit 1
fi
if grep -Eq 'dentry_put\\(parent\\)' kernel/fs/vfs.c; then
  echo '[VFS FAIL] recursive Dentry parent teardown returned' >&2
  exit 1
fi
