# OpenFile data-plane contract (NAS-A002)

This document records the actual migration contract for NaSeed's shared
open-file descriptions. It is **not** a claim that all Linux I/O semantics
or the broader VFS architecture are complete.

## Identity, lifetime and direction

`nas_fd_entry` is a process-local handle; its `slot` refers to one refcounted
`nas_open_file`. `dup`, `fork`, and `SCM_RIGHTS` can share that object, which
owns flags, seek position, the pinned VFS inode/Path where applicable, and
`nas_file_ops`. The final reference closes the provider once. **Do not create
a second File object for each filesystem or process FD.**

For `read(2)` and `write(2)` the ownership path is:

1. `kernel/fs/file_io.c` resolves process FD to an owned OpenFile slot,
   checks access mode, and caps the Linux logical transfer at `MAX_RW_COUNT`.
2. The provider's optional, *non-consuming* `read_plan`/`write_plan` examines
   the **original requested byte count**, returning an allowed budget or an
   immediate error/EOF; it may not mutate file contents, offsets or queues.
3. The syscall adapter validates and copies user pages through MM usercopy,
   using page-local, bounded kernel scratch buffers. Before a consuming read,
   it prepares the destination's write/COW mapping; before a write, it reads
   the user source. Kernel providers never see user virtual addresses.
4. The provider's `read(slot, kernel_dst, n)` or
   `write(slot, kernel_src, n)` transfers at most `n` bytes and commits its
   own state only for bytes actually transferred. Partial results take
   precedence over errors after earlier successful chunks.

One `read_plan`/`write_plan` call covers the whole logical operation;
individual provider callbacks may then receive smaller chunks. In the
current non-preemptible, single-core kernel, no task can interleave mutation
between admission and commit. An SMP implementation **must** add appropriate
per-OpenFile/provider synchronization; the present plan is not a reservation
or lock.

## Semantics that define the boundary

| Provider | Non-consuming admission | Commit / failure rule |
| --- | --- | --- |
| Pipe and AF_UNIX stream | Decide using original count; `<= PIPE_BUF` must fit atomically | Preflight all user source pages for admitted atomic writes; never consume queued input before destination COW validation; preserve `EPIPE`, `SIGPIPE`, and partial `> PIPE_BUF` writes |
| Eventfd | Reads accept count `>= 8` but move eight bytes; writes require count **exactly eight** | Linux consumes the counter even when a subsequent read `copy_to_user` fails with `EFAULT`; rejected writes do not increase it; preserve wait/nonblock/overflow behavior. This is a **documented commit-before-copyout exception** to the usual read staging rule |
| PTY and physical TTY | Preserve canonical, noncanonical, EOF and available-byte distinctions | Preserve line-discipline peek/commit, ISIG/NOFLSH, VMIN/VTIME, peer close and COW no-data-loss behavior |
| Mounted regular file | Read through opened/pinned VFS inode, not user FD; trim preflight at EOF **only if** `read_size_is_eof` is advertised by that filesystem | Shared OpenFile offset changes only by successful bytes, `O_APPEND` obtains the current size from the provider. Dynamic `/proc` can report `st_size=0` yet return content |
| Null, zero and legacy Limine | Null has immediate EOF; zero produces bytes; legacy module bounded by size | The common `access_ok` address-range check still runs, **even at EOF and for discarded writes**. Null and zero write sinks do not dereference unmapped but in-range user pages (`write_ignores_source` capability) |

`ioctl`, pathname lookup, FD creation/close, `pread/pwrite`, and mmap file
reads have distinct control/positional interfaces. Do not mechanically
rewrite them as stream FileOps. Physical TTY's waiter-specific VMIN/VTIME
deadline currently reads the selected Task wait state; that is a narrow
exception, not permission to inspect process FD tables or user pointers.
The `read_size_is_eof` flag is a property of the **filesystem provider**,
not a string match against its name. Bootfs, tmpfs, ramfs, littlefs and
lwext4 publish accurate sizes; procfs remains unbounded by `st_size`.

## Regression and outstanding work

### NAS-A008 vector transaction extension

`readv` and `writev` are **logical I/O requests**, not necessarily loops over
`read` and `write`. Their shared importer checks the process FD, OFD access
direction, original iovec metadata, cumulative lengths, and every user address
*range* before permitting any provider mutation. Up to Linux's 1024 iovecs
are snapshotted in temporary kernel heap memory, not a 16-KiB kernel stack
array. Imported metadata is independent of later user-buffer writes.

The FileOps `aggregate_readv` and `aggregate_writev` capabilities are
independent: an aggregate path invokes one read/write plan for the whole
vector, then copies through bounded kernel buffers across iovec boundaries.
A segmented path retains the scalar provider behavior only **after** the
entire iovec array has been imported and validated. No FD kind tests or user
addresses enter a provider callback.

| OpenFile provider | readv policy | writev policy | Linux-specific reason |
| --- | --- | --- | --- |
| Pipe | aggregate | aggregate | Shared queue admission; a writev totaling at most `PIPE_BUF` cannot partially publish when insufficient capacity or a later source page is invalid |
| AF_UNIX stream | aggregate | aggregate | Same kernel pipe transport and transaction budget |
| Eventfd | aggregate | segmented | Linux implements `read_iter` but falls back to scalar `write`: `readv([4,4])` succeeds; `writev([4,4])` and leading `[0,8]` fail `EINVAL`, while `[8,8]` adds two values |
| TTY, PTY and existing mounted files | existing segmented fallback | existing segmented fallback | Their EOF, termios, short I/O, append and provider-open semantics require their own independent oracle before opting into aggregation |

This vector migration is staged separately from the earlier scalar NAS-A002
cutover. Linux-host oracle and a production-provider host fixture can verify
the architectural corner cases, but **do not** prove guest libc/syscall ABI
or three-ISA QEMU success. `NAS-A008` stays open until exact-head native
checks cover the new vector path and the scalar path still passes.

The boundary guard in `scripts/check-architecture-boundaries.py` checks that
data-plane provider *functions* do not regress to direct usercopy or
process-local FD lookup, while allowing legitimate usercopy in ioctl and
syscall control-plane entrypoints. Mutation self-tests must stay green.

- Host: M106 real pipe/socket FileOps + page fault/COW/atomic-EFAULT; M105
  PTY Linux oracle/NOFLSH; M102 TTY COW; M89 poll and M62 lifetime.
- Target: compile each ISA with its actual toolchain, then run three-ISA QEMU
  M55 eventfd/epoll, M38 readv, M57 large-file/writev, and shell/IPC flows.
- Only an **exact-head run** proves a full native gate. Host success does not
  imply cross-ISA code generation, guest completion or manual full success.

Two related but separate debts remain: first-block and wakeup-retry policy
still enumerate FD kinds in `kernel/syscall/dispatch.c` and
`kernel/task/blocked_completion.c`; the physical TTY wait path also needs
per-waiter readiness. These should converge to an explicit provider wait
contract after preserving both sides' `SIGPIPE` and signal semantics. Also,
NAS-A003's per-open backend handle is not implemented merely because FileOps
now accepts the shared OpenFile slot.
