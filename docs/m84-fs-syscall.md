# M8.4 — NaOS-style filesystem syscall ownership

## Change
- Move the **working** FD, pipe, CPIO bootfs, writable tmpfs, path, directory,
  stat/readlink, poll, fcntl and dup syscall adapter bodies from m1.c into
  `fs/syscall.c`. Preserve the existing underlying fs/FD backend modules.
- Common m1 syscall dispatch now asks `nas_fs_syscall(args)` once for known
  filesystem syscall IDs. The typed `handled` result distinguishes an
  unsupported non-FS syscall from a filesystem call that returns `-ENOSYS`
  or any other Linux error; no fake success/fallback.
- Read/write/writev intentionally still enter scheduler-aware
  `task_pipe_io()` for blocking/replay and call the existing
  `nas_fs_fd_read/write/writev` implementations through fs/io.h.
- `execve` calls `nas_fs_resolve_path` for its initial pathname, not a
  private m1 function.
- `uname` and process exit/debug trace remain in the common syscall image.
- Shared-description refcounts, pipe EOF and SIGPIPE, tmpfs rename/unlink
  lifecycle, and CPIO read-only behavior are unmodified.

## Limits
- Linux syscall number and `struct stat` layout are still **x86-64 specific**;
  the next ABI stage must choose arch syscall-number maps and stat encoders
  independently. This is not yet portable multi-ISA syscall dispatch.
- No VFS object redesign, driver model, runtime plugin, scheduler replacement,
  or new filesystem is introduced.
- Must pass original full 34-step QEMU real Bash/BusyBox/Alpine/ash,
  fork/wait, FD duplication, SIGPIPE/pipe EOF, exec, shebang and tmpfs tests
  before merge.
