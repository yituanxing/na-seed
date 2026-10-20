# M6.9 — original Alpine BusyBox cp/mv on a bounded RAM /tmp

Actual `cp` and `mv` workloads drive this milestone: a path rename is
not file copying, and an overwritten destination must remain alive as an
unlinked inode while any open file description still owns it.

Linux x86-64 `rename`, `renameat` and `renameat2` (only flag zero and
`RENAME_NOREPLACE`) move inode namespace entries without duplicating file
data. For directory moves the fixed-sized pathname tree and task cwd paths
are updated together after validating all new path lengths. A same-name
rename is a no-op. Existing regular targets are atomically replaced,
retaining the replaced inode if any fork/dup descriptor still references it.
Mismatched destination types, nonempty destination directories, missing
parents and cross-mount attempts return explicit errno. /etc and the rest
of CPIO remain immutable. The finite /tmp overlay retains eight inodes,
16 KiB per regular file, and never silently wraps/truncates excess writes.

The applet links /bin/cp and /bin/mv reference the unmodified pinned Alpine
BusyBox binary. QEMU tests cover copying original CPIO content, an 8192-byte
copy through a real pipe, moving files and nested directories, replacement
while old inode remains open, cwd tracking, no-clobber, readonly CPIO and
bounded capacity errors. All M0–M6.8 regressions remain required.

**Scope:** not a full Linux VFS; no persisted files, hardlinks or generic
writable mount points, and symlink-plus-dotdot semantics are not complete.
