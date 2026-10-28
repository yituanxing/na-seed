# VFS lifecycle: Linux and NaOS architectural cross-check

Compared with Linux kernel `Documentation/filesystems/path-lookup.rst`
(https://docs.kernel.org/filesystems/path-lookup.html) and NaOS
`kernel/src/fs/vfs/{vfs.h,vfs_inode.c,vfs_dentry.c,vfs_file.c,vfs_mount.c}`
at `2c45649144afa3dd2e32147d7029e354dc9fc518`.

## Invariants implemented in this batch

- An open file owns an inode, not the pathname that originally named it.
  A backend `getattr` refreshes inode metadata by identity. In particular,
  `st_nlink=0` after unlink remains valid until the final File close.
- The shared mount+inode lease verifies backend incarnation on pin, unpin
  and metadata refresh; a reused inode number must not release another
  live File. `dup`/fork share one open File, cwd has an independent pin.
- The tmpfs provider supplies current metadata. Generic Linux stat ABI
  code no longer guesses `st_nlink=1` for deleted files, nor manually
  decodes tmpfs File metadata for fstat. No filesystem-specific syscall
  was added, and vendored lwext4/U-Boot sources are unchanged.

## Major remaining differences — not yet a Linux-equivalent VFS

1. Linux `struct path` holds both a counted mount and dentry. NaOS has
   independently refcounted `vfs_super_block`, `vfs_inode`,
   `vfs_dentry`, `vfs_mount`, `vfs_file`. NaSeed still dispatches via
   a canonical path-prefix mount table and borrowed inode snapshots;
   it lacks persistent VFS-owned dentries and a proper mount tree.
2. Implement true inode eviction/orphan ownership in the filesystem port
   before removing EBUSY restrictions for lwext4 unlink-open and
   rename-overwrite. A successful name unlink must not destroy an open
   inode's data.
3. Move remaining FD-kind-specific stat, directory, open and path resolution
   through owned generic File/Path operations, preserving independent
   descriptor-local CLOEXEC vs shared File flags/offset.
4. Before SMP: formalize dentry/inode/mount lifetime and safe mount/unmount,
   then add locking/RCU. Do not copy Linux's synchronization complexity
   before its reference ownership contract is correct.

Test these invariants with real mounted providers and three-ISA QEMU;
a passing host fixture alone is not complete acceptance.
