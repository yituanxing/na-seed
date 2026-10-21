# M7.0 — source-first ownership audit and first /tmp module boundary

## Reference design and why this split is now justified

Read `na-kernel` **history**, not merely today's mature VFS.
The early 2025-05-08 kernel (`7ebabd61b`) already had a
`vfs_node` + callback table, despite having far fewer Linux facilities
than current NaOS. Today's `na-kernel` uses separate inode, dentry,
open-file-description, mount and per-task filesystem objects, plus a
pathname resolver. Its `initramfs.c` unpacks CPIO into a writable
tmpfs-backed root. Neither design needs to be transplanted wholesale.

NaSeed took the opposite validated path: original musl/glibc BusyBox
ELFs and real shell commands exposed exactly the abstractions needed.
Now two materially different stores are running: read-only CPIO
(`bootfs.c`) and volatile writable `/tmp`. With mkdir, unlink,
rename, nested directories and inherited FDs already verified, a
single monolithic `m1.c` is no longer an appropriate ownership boundary.

## M7.0 change: preserve behavior, establish one honest boundary

```
Linux syscall ABI / user-copy / task-relative pathname  (kernel/m1.c)
           |  resolved kernel pathname / open-file slot
           v
bounded /tmp: namespace, inode state, tombstone, slot owner (kernel/tmpfs.c)
           |
           +-- RAM contents, mkdir/unlink/rename, recursive directory paths
           +-- refcount-based final-close reclaim, cwd rename fixup
CPIO readonly archive index + symlink resolver            (kernel/bootfs.c)
global open-file-description reference counts              (kernel/task.c)
```

The `tmpfs.h` transitional interface publishes the finite inode record
because current read/write/stat/getdents wrappers still need its metadata
and kernel byte buffer. Unlike the old code, `m1.c` no longer owns the
RAM backing table or slot->inode bindings or mutates the namespace.
No new mount API, superblock, dcache, object hierarchy, heap allocator,
fake filesystem, or syscall was introduced. The constrained backend
remains **eight inodes, 16 KiB per regular file, volatile**.

Open FDs belong to a process (`nas_fd_table`), an open *description*
has shared offset, status and `nas_file_refcount`; its tmpfs inode
binding belongs to the tmpfs backend, persists across fork/dup, and is
cleared only on the final descriptor close. Unlink or overwrite removes
the namespace link immediately, but inode bytes cannot be reused until
all referencing open descriptions are closed. Rename of an open
directory adjusts task cwd representations; today this is a temporary
coupling, **not** a new task or VFS abstraction.

## Explicitly observed limitations / follow-up audit

1. **Path traversal:** `resolve_path` lexically collapses `..`
   before CPIO symlink traversal. Linux requires component-by-component
   resolution; symlink then `..` can produce a different destination.
   Do not claim full `openat`/POSIX pathwalk correctness.
2. **Read-only semantics:** current `sys_open_path` rejects
   `O_RDONLY|O_CREAT` on existing CPIO files with `EROFS`, even
   when no creation is needed. This is incorrect Linux behavior.
   Preserve it during this mechanical extraction, then add a
   dedicated negative/positive syscall test and fix it separately.
3. **Directory cursor stability:** `getdents64` uses an ordinal of the
   currently linked inode list. After unlink/rename, the ordinal can
   refer to a different entry. Linux exposes opaque cookies; avoid
   promising stable iteration under mutation.
4. **Resource budget:** RAM /tmp has 8 inode objects including
   tombstones and fixed-size buffers. `ENOSPC` is a conscious limit,
   not a proxy for a complete tmpfs. Expanding to a heap-backed store
   should follow an actual larger original-program workload.
5. **Lifetime under process termination:** closing inherited or
   duplicated FDs uses shared reference counts. Repeat fork/exec/exit,
   unlink-with-open-FD and failed-open tests should be extended in a
   separate resource-stress acceptance gate.
6. **Other scheduler/ABI gaps:** cooperative syscall-boundary scheduling,
   incomplete signal delivery, poll blocking and absence of actual
   timer preemption still limit Linux userspace. Refactoring filesystem
   interfaces does not fix or mask these deficiencies.

## Acceptance and next decisions

The change is intentionally **mechanical**: full pinned QEMU regression
matrix M0–M6.9 (in particular eight /tmp create/append/copy tests,
eight directory/lifecycle tests and ten cp/mv/rename tests), plus strict
freestanding compile/link and syscall UAPI checks, must pass unchanged
before merge. Next: fix demonstrated syscall discrepancies with explicit
tests; continue moving FD and user-copy glue out of `m1.c` only when
a real repeated dependency justifies it.

If our interfaces become tangled, compare against early and current
NaOS code *at the relevant layer*, borrowing a clear pattern or
implementation with proper provenance/license checks, instead of
inventing an oversized framework in advance.
