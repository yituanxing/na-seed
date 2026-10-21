# M7.1 — FD description lifetime module, borrowed design not copied code

**Source reference:** early NaOS `kernel/src/fs/fs_syscall.c` and
`kernel/src/task/task.h` at commit `7ebabd61b` versus current NaOS
`kernel/src/fs/vfs/vfs_file.c` / `kernel/src/task/task_struct.h`.
NaOS now distinguishes per-process fd entries from refcounted
`vfs_file` open descriptions. NaSeed has the same concrete need from
BusyBox `dup2`, `fcntl`, `fork`, pipes, and opening unlinked inodes.

This is a **mechanical extraction**, not an invented VFS.

- `task.c` owns task registry, PID assignment and process lifecycle.
- `fd.c` now owns shared description refcounts, file position,
  status flags, fork-held references, descriptor table close and dup.
- `m1.c` owns syscall decoding, user-memory access, and backend
  teardown: only after `nas_fd_close` returns its final-description
  slot do pipe and RAM-inode release run.
- `tmpfs.c` owns RAM inode and fd-slot->inode bindings. Nothing in
  `fd.c` reaches into tmpfs or pipe internals.
- The transitional `nas_fd_table` retains the same parallel descriptor
  arrays; a future switch to a `struct open_file_description` must
  demonstrably improve correctness or support a new real workload.

The backend release contract matters: a forked child's exit cannot
reclaim a pipe or inode still referenced by the parent; dup shares
the *same offset*, but `FD_CLOEXEC` remains descriptor-local.

**Acceptance:** strict freestanding build, Linux ABI audits, full
original-program M0–M7.0 QEMU regressions, and additional genuine Alpine
BusyBox ash tests for two reads through the same descriptor, dup-shared
offset, exec/inheritance, unlink-and-recreate while an FD remains open,
and slot recycling on final close. Any failure blocks merge.

**Explicit debt:** `sys_open_path` still has incorrect CPIO
`O_RDONLY|O_CREAT` semantics, and path component traversal through
symlink+`..` is incomplete; neither is silently changed here.
Once this refactor is accepted, fix such semantics using standalone
positive/negative acceptance tests before adding new subsystems.
