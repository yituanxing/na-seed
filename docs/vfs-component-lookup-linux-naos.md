# VFS component lookup batch — architectural contract

NaSeed PR #146 now changes the **actual filesystem lookup ABI** across
bootfs, tmpfs, procfs, lwext4, LittleFS and host VFS fixtures.

- A provider implements `root(mount, out)` and
  `lookup_child(parent_inode, basename, out)`. VFS alone selects the
  mounted instance, validates component boundaries and implements
  /tmp's explicitly flagged CPIO fallthrough.
- Bootfs uses archive parent-child inode links; tmpfs searches its own
  dentry namespace by parent inode; procfs resolves dynamic children from
  the parent proc-inode identity and its real process table; lwext4 uses
  original upstream directory iteration **by parent inode** and
  `ext4_fs_get_inode_ref`; LittleFS constructs only a **filesystem-local**
  relative path from its own stable identity record. Neither filesystem
  receives a global `/mnt/...` path for lookup.
- The old absolute-path provider `lookup` callback is removed; this is a
  real ABI replacement, not a parallel adaptor silently preserving it.
- Read/write/fsync/getattr continue to operate on the open inode identity;
  no vendored lwext4/LittleFS/U-Boot source is edited.

### Still structural work, not falsely claimed complete

The VFS mount table still selects a mount by longest absolute component-
aligned prefix, not an owned mountpoint dentry. Directory operations and
lwext4 namespace mutations still accept pathname arguments, and tmpfs
still keeps **provider-local** dentry names as absolute strings. VFS namei
retains its bounded string representation and repeated prefix lookup,
rather than a persistent `struct path` holding mount+dentry refs.
Those need one subsequent coherent owner/lifetime migration, not more
filesystem-kind branches in generic code.

Linux reference: `Documentation/filesystems/path-lookup.rst` and
NaOS reference at `2c45649144afa3dd2e32147d7029e354dc9fc518`:
`kernel/src/fs/vfs/vfs_lookup.c`, `vfs_dentry.c`, `vfs_mount.c`.

### Regression contract

Keep old three-ISA QEMU acceptance, host real ext4/LittleFS fixtures and
the actual tmpfs/procfs provider fixture. The latter also mounts the same
mock provider twice under separate mount records and asserts inode
identity, distinct mount/device ownership, and EXDEV for cross-mount
operations. Do not merge on compile-only evidence.
