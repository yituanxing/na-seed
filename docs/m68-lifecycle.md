# M6.8 — BusyBox directory and unlink lifecycle, no generic VFS

**Real workload:** unmodified Alpine BusyBox ash runs
`mkdir /tmp/work; echo hello > /tmp/work/test; cat /tmp/work/test;
rm /tmp/work/test; rmdir /tmp/work`.
This requires Linux `mkdir/mkdirat`, `unlink/unlinkat` and `rmdir`
semantics, a writable directory tree, and directory-FD handling.

Extend the bounded RAM-backed /tmp inode table (eight entries, 16 KiB
per file) with directories and linked/unlinked states. Paths are resolved
relative to existing parents; `ENOTDIR`, `ENOENT`, `EEXIST`,
`ENOTEMPTY`, `EISDIR`, `EROFS` and bounded `ENOSPC` errors
are returned rather than fabricated success. Inodes survive unlink while
any open-file description still references them, even through fork/dup,
and are reclaimed on final close. `getdents64`, `fstat`,
`newfstatat`, relative dirfds, `chdir` and `fchdir` cover
the volatile directory tree. CPIO still provides the immutable root,
ELFs, musl and the empty /tmp mountpoint.

### Genuine program acceptance
All scenarios boot QEMU with unmodified, pinned Alpine BusyBox/musl:
basic mkdir/write/read/rm/rmdir; nested mkdir -p and rm -r;
chdir plus relative file creation; listing; rmdir nonempty;
unlink while an FD still reads the old inode and a recreated pathname
points to a different inode; forbidden CPIO mutations;
missing-file errors. Existing M0–M6.7 tests remain mandatory.

### Intentional limits
No persistence, generalized VFS, inode permissions/ownership checks,
hardlinks, rename, symlink creation, or full POSIX pathname traversal.
The finite in-memory table is not advertised as a complete tmpfs.
