# VFS component lookup batch — architectural contract

NaSeed PR #146 now changes the **actual filesystem lookup ABI** across
bootfs, tmpfs, procfs, lwext4, LittleFS and host VFS fixtures.

- A provider implements `root(mount, out)` and
  `lookup_child(parent_inode, basename, out)`. VFS alone selects the
  mounted instance, validates component boundaries and implements
  /tmp's explicitly flagged CPIO fallthrough.
- Bootfs resolves a child through its own immutable inode's archive
  path hash (not a full VFS pathname); tmpfs searches its own
  dentry namespace by parent inode; procfs resolves dynamic children from
  the parent proc-inode identity and its real process table; lwext4 scans
  upstream ext4 directory entries **by parent inode** and validates the
  resulting child inode against the public path-based raw metadata API.
  LittleFS constructs only a **filesystem-local** relative path from its
  own stable identity record. No provider receives a global `/mnt/...`
  pathname from the VFS lookup API.
- The old absolute-path provider `lookup` callback is removed; this is a
  real ABI replacement, not a parallel adaptor silently preserving it.
- Read/write/fsync/getattr continue to operate on the open inode identity;
  no vendored lwext4/LittleFS/U-Boot source is edited.

### Still structural work, not falsely claimed complete

The VFS mount table still selects a mount by longest absolute component-
aligned prefix, not an owned mountpoint dentry. Directory operations and
lwext4 namespace mutations still accept pathname arguments, and tmpfs
still keeps **provider-local** dentry names as absolute strings. VFS namei
now walks one component once, carrying the current inode and re-resolving
only on `..`, symlink restarts or the bootstrap /tmp overlay fallback.
It retains a bounded canonical string for path output, rather than a
persistent `struct path` holding mount+dentry refs.
Those need one subsequent coherent owner/lifetime migration, not more
filesystem-kind branches in generic code. The public lwext4 API does
not expose an inode-number metadata read without its private mountpoint
structure: until a pinned, versioned public adapter exists, its provider
temporarily reconstructs the current pathname from the parent ext4 inode,
then requires that the public metadata lookup returns the SAME inode
number as the directory iterator. This is a recorded performance and
rename-race limitation, not claimed as Linux-equivalent inode lookup.

Linux reference: `Documentation/filesystems/path-lookup.rst` and
NaOS reference at `2c45649144afa3dd2e32147d7029e354dc9fc518`:
`kernel/src/fs/vfs/vfs_lookup.c`, `vfs_dentry.c`, `vfs_mount.c`.

### Regression contract

Keep old three-ISA QEMU acceptance, host real ext4/LittleFS fixtures and
the actual tmpfs/procfs provider fixture. The latter also mounts the same
mock provider twice under separate mount records and asserts inode
identity, distinct mount/device ownership, and EXDEV for cross-mount
operations. Do not merge on compile-only evidence.

### Mount ancestry hardening

VFS now rejects registering a mount under a parent that resolves to a
regular file; VFS step lookup rejects crossing any mount with a non-directory
parent. In the transitional CPIO-less direct-ELF bootstrap, an ENOENT
parent is still allowed because /mnt may exist solely as a mounted
filesystem. Namei preserves non-ENOENT root/.. errors rather than silently
treating I/O failures as a missing archive root. The host provider fixture
checks the failure paths alongside nested mount traversal and escaping
a child mount through '..'. These are safety checks, not an assertion
that a refcounted mount/dentry tree already exists.

### Pathwalk and mount fail-closed invariants

Do not equate lexical normalization with directory traversal: every dot or
parent component requires a real directory inode, including `/file/.` and
`/file/..`. Both `nas_fs_lookup_node` and `nas_fs_resolve_existing` now
reject nonexistent roots in the direct-ELF configuration. At mount registration,
validate the inode covered by the mountpoint as well as its parent; reject
mount-on-regular-file and missing non-root ancestors. The only bootstrap
exception is a missing `/` when mounting a disk directly below root without
CPIO. Host fixtures cover positive nested mounts and these negative cases.

### User path normalization must not bypass namei

A later audit found that `nas_fs_resolve_path` historically used the
lexical `nas_path_resolve` helper, which could erase `/file/..` before the
VFS ever inspected the non-directory inode. The namespace syscall adapter
now routes paths containing `.` or `..` through the same VFS component
resolver first; if only the final basename is absent, it resolves the raw
parent inode (and any symlinks) before constructing the canonical leaf.
Ordinary device paths temporarily retain the old lexical ABI until a real
devfs provider exists. This does not replace a counted dentry/mount tree.
