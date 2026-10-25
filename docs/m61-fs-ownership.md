# M61 — FS syscall ABI ownership split (no behavior changes)

Reference boundaries: Linux VFS separates pathname/inode work from open
file-description operations; NaOS places vfs_lookup, vfs_path, vfs_file,
fs_syscall and AF_UNIX socket support in distinct components. NaSeed borrows
the responsibility split, **not** the complete Linux mount/dcache model.

## Established dependencies (three architectures use exactly the same TUs)

```
Architecture syscall number / Linux wire format
    -> syscall/dispatch.c
    -> fs/syscall.c                  read/write, descriptor I/O, stat/poll/ioctl
       -> fs/namespace_syscall.c   relative dirfd, path normalization,
                                     mkdir/unlink/rename/symlink
       -> fs/socket_syscall.c      Linux AF_UNIX socket ABI, SCM_RIGHTS,
                                     ucred and user pointer validation
           -> fs/unix_socket.c     endpoint/listener/peer/rights lifetime
           -> pipe.c              stream byte transport
       -> fd.c                     shared open file descriptions
       -> fs/lookup.c             CPIO/tmpfs lookup and symlink traversal
       -> bootfs.c / tmpfs.c      backend objects and bytes
```

This is a mechanical owner extraction of existing production code: all
AF_UNIX and namespace syscall case expressions, checks, return values,
backend calls, and one-VM-per-task rules are preserved. The only symbol
adaptation is replacing one private `tmp_by_fd` use with its identical
`tmp_by_slot(file_slot[dirfd])` expression, and reusing the shared kernel
`nas_fail` implementation for unreachable post-validation socket failures.
Both owner dispatchers return `handled=false` for foreign operations;
unknown Linux syscalls still return ENOSYS from the central dispatch.

Non-goals: no artificial dentry/inode cache, mount tree, SMP locks, full
network stack, new sockets or fake syscall success. The per-task FD triplet,
path-bearing tmpfs inode and 1,966-line FS adapter are further convergence
work: improve these with their own genuine regression fixtures, not blind
search-and-replace. The original M58/M59/M60 real Bash, rsync and tmux
three-ISA workflows are the primary regression gate; always retain the
previous native grouped, ARM64 and userspace test suites.
