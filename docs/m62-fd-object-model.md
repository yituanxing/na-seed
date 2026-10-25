# M62 — shared FD-entry object model

NaSeed now has three distinct owners instead of three parallel per-task arrays:

```text
Task / CLONE_FILES descriptor table
   nas_fd_entry[64] = {kind, slot, cloexec} — copied by fork
                |
                v
   nas_open_file[128] = {refcount, status_flags, position, ops, backend handle}
                |
                +-- bootfs, tmpfs, pipe, eventfd, epoll, PTY, Unix socket
```

Linux VFS separates a per-process descriptor table from the shared struct
file/open-file description and inode. NaOS similarly separates task FD
ownership from `vfs_file`, `vfs_inode`, lookup, and backend filesystems.
M62 adopts the **ownership** boundary, not a full dcache, mount tree or
preemptive kernel locking.

All direct legacy `descriptors[]` / `file_slot[]` / `fd_cloexec[]`
accesses now reference one `entries[]` structure (3 bytes × 64).
No shadow/compatibility parallel arrays exist. Source users across boot,
exec, fork, FD, regular IO, poll, socket, epoll, eventfd and PTY share it.

The FD-owner `fd.c` closes by clearing the **single entry atomically at
the code level** and duplicates by copying one entry, then replacing its
descriptor-local CLOEXEC bit. It still performs final-description reference
release separately from the backend callback. No claim of SMP atomicity.

Validation uses a host unit test linked against the real kernel `fd.c` to
exercise duplicated aliases, copied fork tables, shared CLONE_FILES tables,
CLOEXEC independence, shared offsets, exhaustion/invalid slots, and final
backend release responsibility, plus complete real three-ISA QEMU suites.

Future direction: make open-description slot reservation/installation a
central FD operation, and then make file/inode identity independent of
pathname-only tmpfs metadata. Persistent storage and mount namespace remain
out of scope, as do fake Linux syscall success paths.

## M62 owner-consolidation batch

The new `fd.c` functions `nas_fd_reserve`, `nas_fd_install_new`,
`nas_fd_attach_owned` and `nas_fd_first_unused` centralize exactly-once
placement and description reference accounting. Callers reserve before
creating tmpfs/PTY/pipe backend state, install only on success, and transfer
already-owned AF_UNIX accept/SCM_RIGHTS slots without another reference.
The common fd.c layer does not import any backend lifetime logic.
