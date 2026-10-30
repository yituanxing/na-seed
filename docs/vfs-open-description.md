# Shared FD, OpenFile and VFS ownership

Source audit: `vfs-block-lwext4-foundation@0367d01` (2026-10-30).
This replaces the early transitional description that incorrectly said NaSeed
had no VFS inode, dentry, mount or path operations. See
[`ofd-data-plane-contract.md`](ofd-data-plane-contract.md) for the separate
read/write, vector-I/O and blocked-completion rules; they are not duplicated
here. NaOS informed the original design, but NaSeed's current ownership must
be read from its own code.

## Identity and ownership, not one object per descriptor

| Object | Owner and lifetime | Borrowers / consumers |
| --- | --- | --- |
| `nas_fd_entry` | One entry in `nas_files.table` maps a process FD to a shared `nas_open_file` slot. `FD_CLOEXEC` is descriptor-local. | `openat`, `dup`, `fcntl`, `close`, poll and syscall adapters |
| `nas_files` | Refcounted FD table (`kernel/fd.c`); `CLONE_FILES` threads share it, ordinary fork copies its entries and holds each referenced OpenFile. | Task and thread-group lifecycle |
| `nas_open_file` | One global refcounted open-file description owns status flags, position, `nas_file_ops`, and optional pinned VFS inode/Path. `dup` and fork share this object, not cloned offsets. | FileOps, `fstat`, `lseek`, `getdents64`, `pread/pwrite`, poll, locks, procfd display |
| `nas_fs_node` lease | An opened mounted inode is pinned by `nas_vfs_node_pin`; its mount and inode identity survive unlink/rename while the provider permits those operations. | OpenFile, mmap/exec positional reads, inode-level VFS operations |
| `nas_vfs_path` | Positive refcounted Dentry and mount identity, independently owned by opened File or cwd context; released with `nas_vfs_path_put`. | `openat(dirfd)`, `fchdir`, relative namei, mounted directory traversal |
| Kernel-held OpenFile reference | A pending blocked I/O operation or queued SCM_RIGHTS message holds the *same* description beyond close/dup2 of the originating user FD. | Task wait continuation or AF_UNIX receive/teardown |

The fixed-capacity `NAS_FD_LIMIT`/`NAS_OPEN_FILE_LIMIT` pools implement a
resource policy, not a second file identity. `nas_fd_install_new` publishes
an FD referencing the canonical slot; `nas_fd_attach_owned` transfers a
preaccounted reference from `accept` or SCM_RIGHTS without incrementing it.
`nas_fd_hold_description` is the extra owner for blocked I/O. A fork without
`CLONE_FILES` increments the references held by its copied table, whereas a
pthread shares the table itself.

## One normal open path and one final release

1. `kernel/fs/syscall.c::open_path_owned` reserves available FD and global
   description slots before any `O_CREAT`/`O_TRUNC` side effect.
2. `nas_vfs_open_path_at` performs the single Path-first namei/open
   transaction: inode lookup or creation, type/permission checks, optional
   truncation, inode pin and owned Dentry/Path. The filesystem receives
   parent inode and child name, not a reconstructed full pathname.
3. The syscall adapter installs one `nas_open_file` and transfers its owned
   vnode/Path into that description. The current non-preemptible kernel does
   not invoke a new provider-open hook between publication and this transfer;
   any future hook with failure modes must finish before FD publication and
   roll back both references on failure.
4. Every ordinary `close`, `dup2` replacement, process teardown, outstanding
   blocked request and undelivered SCM_RIGHTS message drops its own reference.
   `nas_fs_description_final_close` is the **only finalizer**: FileOps backend
   release, shared `flock` release, owned Path put, owned inode unpin and
   slot zeroing, in that order. An inode's `last_inode_unpin` is **not** a
   substitute for per-OpenFile final close.

Mounted `fstat`, `lseek`, `getdents64`, directory-relative operations and
inode-based read/write already use these shared identities. The old claim
that they still require a separate prefix/pathname rewrite is obsolete.
`nas_vfs_type.read_at` remains necessary for inode-positional mmap/exec
and independent VFS consumers; a future FD-specific handle must not delete
that interface merely because regular `read(2)` can reuse an open handle.

## Existing compatibility boundaries must not be silently removed

- `origin_path[256]` is an optional diagnostic/display hint, but
  `kernel/fs/procfs.c::nas_proc_readlink` still consumes it for
  `/proc/<pid>/fd` text. It is **not** an authority for reopening a file.
- `vpath` is used for dirfd and cwd operations; `vnode` is independently
  pinned for inode I/O and locking. Both references have real consumers.
- `boot_file` remains the original unmounted Limine module compatibility
  path; `/dev` synthetic endpoints remain until a genuine devfs replaces
  them. These do not authorize another generic filesystem namei path.
- The existing `/tmp` and `/proc` fallback-on-miss mounts support specified
  early CPIO/bootstrap cases. Genuine mounted disk filesystems do not use
  that fallback. Deleting it requires separate original-guest acceptance.

## NAS-A003: optional provider-owned per-open state

The provider interface currently supplies inode-based `read_at/write_at`;
`nas_open_file` has no general backend handle. This is a **design limit**,
not proof that each filesystem needs another File object. Genuine littlefs
currently performs `lfs_file_opencfg`/seek/read/close on each `read_at` call.
By contrast, lwext4 already builds positional `ext4_file` access from the
stable inode and does not reopen an obsolete pathname. Do not duplicate the
whole VFS File layer or force an additional lwext4 handle just to share code.

Only introduce an optional OpenFile-owned provider handle when real workload
or measured cost justifies it. The insertion point is the single open
transaction, **before FD publication**; all `dup`/fork/SCM_RIGHTS/blocked-I/O
owners share it, and the existing finalizer releases it exactly once before
the inode/Path pins. The failure path must release a partially opened backend
and both VFS references without publishing a descriptor. Validate at least
littlefs and an independent inode-only provider such as lwext4, including
failed open, final-close, mounting, aliasing and original three-ISA guests.

Separate remaining work includes broader Linux semantics, file-backed shared
mapping/page-cache coherency, capacity growth and SMP synchronization. Their
absence must not be mistaken for the already implemented VFS ownership model.
