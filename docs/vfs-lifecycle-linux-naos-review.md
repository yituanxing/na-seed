# VFS lifecycle: Linux and NaOS architectural cross-check

Compared with Linux kernel `Documentation/filesystems/path-lookup.rst`
(https://docs.kernel.org/filesystems/path-lookup.html) and NaOS
`kernel/src/fs/vfs/{vfs.h,vfs_inode.c,vfs_dentry.c,vfs_file.c,vfs_mount.c}`
at `2c45649144afa3dd2e32147d7029e354dc9fc518`.

## Invariants implemented in this batch

- An open file owns an inode, not the pathname that originally named it.
  A backend `getattr` refreshes inode metadata by identity. In particular,
  `st_nlink=0` after unlink remains valid until the final File close.
- The shared mount+inode lease verifies backend incarnation on pin, unpin
  and metadata refresh; a reused inode number must not release another
  live File. `dup`/fork share one open File, cwd has an independent pin.
- The tmpfs provider supplies current metadata. Generic Linux stat ABI
  code no longer guesses `st_nlink=1` for deleted files, nor manually
  decodes tmpfs File metadata for fstat. No filesystem-specific syscall
  was added, and vendored lwext4/U-Boot sources are unchanged.

## Major remaining differences — not yet a Linux-equivalent VFS

1. Linux `struct path` holds both a counted mount and dentry. NaOS has
   independently refcounted `vfs_super_block`, `vfs_inode`,
   `vfs_dentry`, `vfs_mount`, `vfs_file`. NaSeed still dispatches via
   a canonical path-prefix mount table and borrowed inode snapshots;
   it lacks persistent VFS-owned dentries and a proper mount tree.
2. Implement true inode eviction/orphan ownership in the filesystem port
   before removing EBUSY restrictions for lwext4 unlink-open and
   rename-overwrite. A successful name unlink must not destroy an open
   inode's data.
3. Move remaining FD-kind-specific stat, directory, open and path resolution
   through owned generic File/Path operations, preserving independent
   descriptor-local CLOEXEC vs shared File flags/offset.
4. Before SMP: formalize dentry/inode/mount lifetime and safe mount/unmount,
   then add locking/RCU. Do not copy Linux's synchronization complexity
   before its reference ownership contract is correct.

Test these invariants with real mounted providers and three-ISA QEMU;
a passing host fixture alone is not complete acceptance.

## Unify Linux ABI stat and inode metadata ownership

The same VFS inode node now supplies fstat and fstatat for bootfs, tmpfs,
procfs, lwext4 and LittleFS. Linux fd-based chmod and inode timestamps
share VFS inode operations rather than manually consulting tmpfs FD slots;
tmpfs now retains nanosecond timestamps in the common borrowed inode view.
Only legacy synthetic character/PTY/socket and non-VFS Limine module FDs
keep the old device-specific stat ABI. Writable lwext4 chmod/utimens remain
unimplemented until a safe inode-based upstream API exists; the generic VFS
returns an explicit unsupported operation rather than silently rewriting
an untrusted pathname.

### Shared ext4 inode size over independent opens

The ext4 port's positional writer and truncate handle now derive their live
file size from the VFS inode lease; `ext4_fwrite` must not consume a stale
`node->size` captured by an earlier independent File. The growth loop
uses its actual current offset. The authentic ext4 host test now appends via
a stale second File, verifies the first File observes appended data/size,
then truncates back before original content, unlink-open and rename gates.
This is no substitute for orphan support or a full owned dentry tree.

### Counted parent-mount attachment (2026-10-28 Linux/NaOS audit)

The mount registry previously stored only a prefix string and File/cwd open
count. In Linux `struct path` has a counted mount+dentry, and NaOS explicitly
binds `mnt_parent` to `mnt_mountpoint`; the matching NaSeed seam now stores
the owning parent mount and its covered inode identity at attach time, plus
an independent child-mount reference. Unmount refuses open owners OR children,
while exact-component mount crossings validate the actual parent mount and
inode incarnation. Registering beneath a symlink-alias parent is rejected
until VFS has owned dentry alias semantics; CPIO-less direct-ELF boot with
no root retains the explicitly scoped unanchored `/mnt` bootstrap.

This is **not yet** a counted generic dentry tree, superblock cache or a
per-FD `struct path`; prefix matching is still the conservative interim
mount locator, while the attachment's actual parent identity is now checked.

### /proc reads are VFS inode operations, not a syscall FD-kind implementation

Following Linux File/inode operations and NaOS f_inode/f_op, a single
inverse of `nas_proc_ino` now decodes proc directories, static/pid/TID
regular files and FD symlinks, including descriptor 63. `/proc` supplies
`read_at(node,offset)` like every other provider; Linux read/pread reuses
its shared open-description position and the common usercopy path, not a
second hardcoded syscall-only proc reader. The common positional I/O adapter
now dispatches using the opened provider's read/write capability rather
than enumerating tmpfs/archive/bootfs kind labels. Original legacy Limine
module files remain an explicit non-VFS compatibility exception.

Decoder contract: the typed `nas_proc_directory_from_ino` and
`nas_proc_fd_link_from_ino` wrappers validate into a temporary, and
publish only after kind validation. Failed typed lookups must never overwrite
a live open inode view (e.g. the neighboring fdinfo inode of a procfd link).

### Generic directory-owner path API for procfs, tmpfs, ext4 and bootfs

The syscall namespace adapter no longer branches by `FD_PROC_DIR`,
`FD_TMP_DIR` or `FD_DIR` for relative directory lookup or fchdir.
Procfs now provides the same `directory_path(inode)` callback and
identity revalidation as every mounted directory provider. `fchdir(fd)`
takes a NEW inode+mount lease from the already opened File before dropping
the old FS cwd lease; it must never reopen its origin name after rename.
`/proc/self` is normalized to the PID inode's stable directory path.
This change is common to all three ISAs and keeps the original filesystems
untouched. The interim live-directory-to-string reconstruction remains and
will disappear when counted dentries and mount path objects become owners.

### Generic File offset/size/truncation dispatch

The Linux ABI `lseek` and `ftruncate` were still hard-coded to separate
FD_TMP, FD_ARCHIVE, FD_DIR, FD_PROC_* kind branches even though the common
open description already owns a pinned VFS inode with a shared live size
lease and per-file offset. These now operate on VFS inode capability and
identity for all filesystem providers; only synthetic devices and raw
Limine modules retain exceptions. The three-ISA real ext4 guest suite
checks SEEK_END from a reader File opened *before* another descriptor's
inode growth, then seeks back and reads the new bytes. Full extent of Linux
SEEK_DATA/SEEK_HOLE and directory cookies remains unimplemented and must
not be advertised as complete.

### Real common inode-scoped advisory lock ownership

Linux separates process-scoped POSIX byte-range locks from open-file-
description-scoped `flock` locks; NaOS tracks File and inode lifetime
separately. NaSeed previously embedded *both* locking mechanisms directly
inside `nas_tmp_file`, with a syscall `FD_TMP` gate and a backend array
indexed by FD slots. The shared VFS's refcounted inode lease now owns the
process record-lock table for **every filesystem**, and a bounded generic
flock table keys conflicts by (mount, inode incarnation) and lifetime by the
open File's opaque pointer. Closing any process FD releases its POSIX locks;
flock survives dup/fork until the **last File description** closes, and final
inode unpin evicts any lingering lock owner. The old tmpfs-private flock
fields and algorithm were removed (tmpfs slot-owner remains temporarily for
legacy M63/orphan reclaim). No per-ISA code, no vendor modifications.

### Eliminate the second tmpfs open File owner and duplicate fops

All tmpfs open inodes now rely *only* on the generic VFS inode lease:
`tmp_slot_owner[]`, `tmp_bind_slot`, `tmp_by_slot` and the private
`tmp_release_slot` orphan/lock lifecycle are removed. Filesystem orphan
reclamation occurs when VFS last_inode_unpin calls the tmpfs provider
callback. The isolated M63 test models this real owner boundary. The Linux
syscall File operations for bootfs/tmpfs/procfs/ext4/LittleFS are reduced to
one `ops_vfs_regular` and one `ops_vfs_dir` table; only actual synthetic
devices retain private File ops. No new architecture or vendor code path.

### Single namei-led open(2) control plane — mounted files and directories

Linux `do_filp_open` starts from namespace identity rather than matching
backend-specific pathnames; NaOS likewise binds a File to a VFS inode.
NaSeed's old `nas_fs_open_path` still had parallel tmpfs/procfs/archive
path probes, a second generic pathname resolution, and a third reopen for
the actual FD lease. It now resolves one existing mounted inode with the
common VFS namei, checks UNIX file mode/dir/symlink/open flags on THAT inode,
then pins the exact source inode and installs its shared File. O_CREAT on
a missing final entry dispatches through the same VFS parent inode callback
and performs one lookup for the new inode. The only special handling left in
this boundary is synthetic /dev until devfs, and the unmounted original
Limine module compatibility bridge. No procfs, ext4, tmpfs or bootfs pathname
routing exists in the Linux open adapter. Real three-ISA ext4 guest adds
O_EXCL and O_DIRECTORY checks over an already opened mounted inode.
