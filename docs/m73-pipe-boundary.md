# M7.3 — bounded anonymous pipe implementation boundary

## Source-first design, not an oversized refactor

Looked at the current NaOS `kernel/src/fs/pipe.c` and `pipe.h`.
That project now implements a pipefs-backed VFS inode, page buffers,
wait queues, task wakeups and spinlocks. This is appropriate for
NaOS's scheduler, VFS and concurrency model, but not for NaSeed's
current cooperative syscall-boundary scheduler.

NaSeed already has:
- `fd.c`: open-file-description reference counts and status flags,
  including duplicate/inherited FDs.
- `task.c` / `m1.c`: process switching, syscall replay, SIGPIPE,
  user memory, and end-of-process cleanup.
- A bounded anonymous pipe with 8 objects and 4096-byte ring buffers.

Those facts justify *one* new boundary, not a full pipefs:
`kernel/pipe.[ch]` now owns ring storage, endpoint associations,
EOF/EPIPE/nonblocking capacity decisions, last-description-close
lifetime, and read-only readiness/diagnostic queries. Callers pass
shared description slot IDs; `pipe.c` does not handle user pointers,
syscall frames, process FDs, signals or task scheduling. Conversely,
`m1.c` never accesses the ring arrays or pipe endpoint ownership.

## Correctness contracts

- Endpoints have **separate** shared open-description refcounts.
- `dup` and `fork` keep an endpoint live until *last* close.
- On final close, forget the pipe's old slot **before** it can be
  allocated to a new description; otherwise an old writer may
  be spuriously revived, causing a deadlock.
- Empty read + closed writer is genuine EOF (return zero).
- Full pipe + blocking write is scheduler-blocked then replayed.
  `O_NONBLOCK` receives `EAGAIN`, not fabricated progress.
- Closed reader produces `EPIPE`; SIGPIPE disposition remains the
  task/signal layer's responsibility.
- A write up to PIPE_BUF is atomic relative to available space.
- User-copy checks stay in the syscall adapter. A mapped prefix is
  allowed to produce a partial transfer.

## Validation

The full existing original BusyBox ash pipeline, 4KiB+ streaming,
early-close SIGPIPE, fork/dup/FD lifetime, directory and CPIO
regressions all remain mandatory. The new QEMU checks repeatedly
allocate and release pipes (12 pipelines), run a three-stage
`echo | cat | wc` pipeline, and combine pipe output with volatile
file readback (8 cycles). Actual stdout and clean exit are checked.

## Initramfs/BootFS decision

`scripts/make-initramfs.sh` builds the deterministic CPIO bytes;
Limine delivers the archive as a module and `bootfs_mount` indexes
those bytes **without** unpacking them into tmpfs. This is
conceptually different from another filesystem backend. The
mounting/selected ELF boot path in `m1.c` remains a worthwhile later
boundary, but it is intentionally **not** changed in this pipe-only
refactor. Current `bootfs.c` and `tmpfs.c` stay separate.
