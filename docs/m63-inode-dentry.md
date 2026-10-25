# M63 — tmpfs inode / dentry ownership

NaSeed now separates namespace names from filesystem inode identity:

```
user linkat / renameat / unlinkat / open / getdents64
      │
fs/namespace_syscall.c + fs/metadata_syscall.c    [Linux ABI]
      │
fs/tmp_namespace.c     [pathnames, hardlinks, rename subtree]
      │  dentry {path, inode*} (slab frames, multiple aliases)
      ▼
tmpfs.c                [inode, sparse 2-level pages, POSIX locks]
      │  inode {ino, nlink, mode, size, page-root}
      ▼
fd.c  [open-file descriptions: refcount+offset; after final unlink
       the inode lives until the final open reference closes]
```

This follows the relevant Linux VFS distinction (`dentry` vs `inode`
vs `file`) and NaOS's split between VFS lookup and filesystem backend
but does not create a second dcache, mount engine, persistent FS or
architecture-specific logic.

Behavior supported: multiple hardlink dentries to one regular inode; one
`st_ino` with accurate `st_nlink`, and shared content, permissions, sparse
pages and inode record locks. `link` and `linkat` decode x86-64 and
asm-generic ISA-specific numbers above the shared semantic syscall
dispatcher; `AT_SYMLINK_FOLLOW` follows the old end only. Hardlinking
directories is rejected, read-only CPIO and cross-filesystem mutations do
not silently copy. Renaming an existing alias to the SAME inode is a no-op.

Existing rename tree semantics are preserved: moving a directory updates
all *dentries* beneath it and each task's cwd; other links to files outside
the renamed subtree retain their names. Deleting a name reduces nlink;
it does not truncate the inode until all names and shared FD references
are gone. Path resolution and getdents64 read dentry names; fstat and
fcntl/read/write continue to use the stable inode pointer.

New host ABI-independent regression links the *real* tmpfs.c and
fs/tmp_namespace.c against tiny deterministic frame, bootfs and clock
fixtures. The existing genuine signed Alpine programs on 3 QEMU ISAs
remain the acceptance gate; new syscall decoders follow native x86
__NR_link/__NR_linkat and asm-generic linkat=37.

Never claim M55/M56 slow source-build suites green based on this gate.
