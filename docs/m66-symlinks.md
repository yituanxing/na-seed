# M6.6 — bare BusyBox applets through CPIO newc symlinks

Driven by real Alpine ash: the previously unsupported `cat /etc/hostname`
failed because the read-only archive had only `/bin/busybox`, not `/bin/cat`.
The OS does not special-case command names or silently accept missing paths.

The deterministic CPIO newc fixture now includes actual `S_IFLNK` records
whose data is the relative target `busybox`. The archive index validates
target size, rejects embedded NULs, bounds names and symlink expansions, and
preserves the original symlink inode/mode for `lstat`, `fstatat(...,
AT_SYMLINK_NOFOLLOW)`, `getdents64` (DT_LNK) and `readlink(at)`. Ordinary
`stat`, `openat`, `chdir`, and `execve` resolve links to their real
archive file while keeping the caller's program name for BusyBox applet
selection. Cycles return real `-ELOOP`. Link targets are interpreted after
relative-parent resolution. No duplicate executable bytes are staged.

New QEMU tests use unchanged official Alpine BusyBox and its musl interpreter:
`cat /etc/hostname`, `cat /etc/hostname | wc -c`, large bare-app pipelines
transferring the real 804616-byte ELF, host-matched `sha256sum`, `readlink
/bin/cat`, and `ls -l /bin/cat`. The entire M0–M6.5 regression matrix
remains mandatory, including the shell diagnostic's former PATH blocker,
which now must pass.

**Boundaries:** read-only archive entries, no on-disk filesystem, creation or
mutation of links, no writable tmpfs/VFS, no preemptive scheduler, and no
generic job control. The earlier lexical cwd/dirfd normalizer runs before
archive traversal: complex symlink-plus-`..` paths require a separate
pathwalk correctness audit before claiming complete POSIX path semantics.
