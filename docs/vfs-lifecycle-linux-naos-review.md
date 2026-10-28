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

The now-unneeded per-File proc PID/TGID/kind/fd and bootfs entry pointer
fields were removed from the shared open-description object: inode ownership
is not copied or encoded a second time in an FD. The three-ISA ext4 guest
further proves O_CREAT|O_DIRECTORY on a nonexistent inode refuses without
creating a leftover file. This guard precedes the VFS namespace mutation.

### VFS owns the complete mounted inode open transaction

The Linux/NaOS comparison exposed another boundary leak: after unifying
namei in the Linux open adapter, it still implemented the filesystem's
create/exclusive/truncate/RO/dir/symlink policy and its pin transaction.
These operations now live in `nas_vfs_open_inode(path,options,node)`: one
common VFS control plane for ALL providers, returning an explicitly owned
mount+inode lease on success. The Linux-facing syscall adapter only decodes
per-ISA O_* bits, picks temporary synthetic devices, reserves an FD slot,
installs the shared File, and transfers that lease (or releases it on FD
capacity failure). O_EXCL does not follow a final symlink, while directory
creation guards and mount read-only/capability checks precede mutation.
The original Limine non-VFS module exception remains a narrow compatibility
bridge, not a second file implementation. A real common host VFS fixture
covers pinned open, read-only write refusal, O_DIRECTORY and O_EXCL.

FD reservation for openat remains a *non-mutating preflight* before the VFS
open transaction. This preserves Linux's EMFILE/ENFILE no-side-effect
contract: exhausted FDs must never truncate or create a filesystem inode.

### Owned mount attachment and provider boundary (2026-10-28)

A mount no longer stores only borrowed parent inode identity and child count. It pins the parent directory and, when present, the covered inode through the same VFS lease table as cwd and File. Registration acquires both references before publishing the new attachment; a failed acquisition rolls back without changing the parent child count. Detach releases both in reverse order before removing the entry. This specifically closes tmpfs parent/orphan reuse behind a live child mount. The host VFS fixture asserts covered inode pin and release, parent-mount EBUSY while nested, and last detach cleanup. Provider read results and directory records are checked at the VFS boundary before Linux usercopy. Namei rejects detached/stale parent identities.

This is a counted mount *attachment* and owned covered inode, not a Linux-equivalent dentry cache or mount tree: nested mount identity still uses canonical mountpoint strings, directory FD path reconstruction remains, ext4 last-link orphan reclamation remains EBUSY and block IRQ queues remain polling. Do not merge without exact-head native three-ISA and upstream filesystem regression evidence.

### Mount-incarnation ABA prevention

The counted mount attachment also needs a monotonically assigned incarnation (the already existing VFS device ID). Every borrowed inode returned by root, child and mount-crossing namei now carries it. VFS data/metadata operations, pin/unpin and stat device lookups reject an inode from an old registration even if a mount context is re-registered at the exact same address. This prevents old borrowed inode pointers from crossing to a remounted BlockDevice. The authentic ext4 host fixture now provides concrete, distinct root and covered directory inode identities; a fabricated anonymous inode cannot satisfy an ownership contract.

### Shared VFS namei batch (2026-10-28)

One component-aware namei owner now resolves canonical parent identities, negative final basenames, and checked leaf names. All VFS namespace mutations, syscall path preparation and AF_UNIX pathname resolution use the shared VFS walk rather than a second lexical dotdot algorithm. bootfs, tmpfs, original lwext4 port and original littlefs port use a common validated child-path builder while retaining provider-owned inode-to-path lookup. The old lexical normalizer is deleted from kernel/path.c. Device names without devfs and CPIO-less direct ELF bootstrap retain a contained syscall compatibility fallback; these are not new VFS providers. Host and actual ext4 tests cover canonical symlink parent, missing final leaf, regular-file dotdot traversal and lease table exhaustion with counted mount pins. M50 static architecture CI rejects a duplicate parent pathwalk and old lexical path normalizer. This does not implement POSIX ext4 orphan lifecycle or a full dentry cache.

### Negative directory trailing slash regression (2026-10-28)

The first exact-head native worktree sweep exposed a real BusyBox mkdir -p regression: an absent directory spelled with a trailing slash returned ENOTDIR during syscall path preparation. The central namei candidate transaction now preserves full component checks first, then removes trailing separators only for an ENOENT negative final component. A preexisting regular file with a slash still returns ENOTDIR. The authentic x86/RV64/AArch64 rsync worktree fixture and BusyBox nested mkdir exercise this transition. Genuine upstream LittleFS and lwext4 guest object link contracts now explicitly allow nas_vfs_join_child to resolve in the one common VFS owner, without copying any implementation into the port.

### Parent-inode tmpfs dentries (2026-10-28)

The writable tmpfs namespace is now a **dentry edge graph**: (parent inode, one basename) -> inode. It no longer stores a full absolute pathname in every dentry. The VFS tmpfs provider performs create, mkdir, unlink, rename, hardlink, symlink and readdir against the same parent inode + basename contract as lookup_child; the previous tmp_child_path reconstruction and recursive copying of every descendant pathname on rename are deleted. Moving a directory changes one dentry edge (parent pointer + basename), so descendants retain stable inode identity and their paths are derived through the parent chain. Cycle checks, subtree maximum path checks and Task.cwd string cache migration are preflighted before publishing that edge change. Path-based legacy wrappers still exist solely for AF_UNIX bind and the isolated host inode unit test, implemented by traversing the same dentry graph; no second namespace table exists. The bootfs /tmp overlay root remains a borrowed immutable inode, and inode reclamation still happens on the final VFS pin release. The M50 architecture audit forbids restoring absolute-path storage to dentry and old tmp_child_path providers. This is an inode-owned tmpfs dentry tree, not yet a global cached Linux dcache across all backends, and ext4 orphan recovery remains separate.

### Borrowed inode reuse after unlink / safe validation (2026-10-28)

File/cwd pins already block actual tmpfs reclamation, but a transient borrowed inode returned by namei is not an owned reference. Once the last pin and last link disappear, the tmpfs slab address can be recycled; comparing pointer and mount number alone is not sufficient when the lease table is empty. The optional VFS filesystem validate_inode hook verifies a live backend incarnation before ANY read/write/stat/namei/pin. The tmpfs provider checks its existing monotonic inode number and directory type, including the foreign immutable CPIO /tmp root. The VFS first resolves a borrowed mount pointer against the attached registry (and checks mount generation) BEFORE dereferencing it, preventing use-after-free on old mount descriptor snapshots. flock/POSIX lock owner queries and directory enumeration follow the same validation. Unpin deliberately does not revalidate the soon-to-be-reclaimed orphan, so final File release remains possible. Host tests simulate slab-pointer reuse and confirm ESTALE before provider I/O. This is still not a general ext4 orphan lifecycle or global dentry cache.

### Real inode-lease saturation fixture / cleanup (2026-10-28)

The mandatory tmpfs validate_inode check also applies to the host VFS lease-capacity fixture. The previous test faked distinct st_ino numbers while reusing the same backend inode pointer, which is no longer a valid inode incarnation. The new fixture creates separate host backend inode records for every table slot, preserving the resource-exhaustion/O_TRUNC/O_CREAT no-side-effect acceptance under the stronger contract. The unused tmp_parent() lexical pathname helper and header declaration are removed; legacy AF_UNIX wrappers still walk the parent-pointer dentry graph via legacy_parent(), and VFS providers never reconstruct absolute names.

### VFS-owned mount superblocks and stable attachment slots (2026-10-28)

The previous mount registry stored a borrowed caller mount descriptor, carried its mutable mountpoint path through namei and moved every later slot on unmount. That is not a safe identity for File/cwd/inode leases or nested mounts. The VFS now privately snapshots each mount descriptor and path into a **fixed, non-compacting attachment slot**, each with a stable superblock address. The original descriptor pointer is only an opaque *registration token* for unmount. Each node's mount pointer refers to this stable internal superblock and its monotonic device incarnation; on removal the slot is cleared and an eventual reuse does not validate stale borrowed nodes. The mount registration acquires parent and covered inode pins before atomic publication and releases them on detach. The real mount crossing path matches a **parent mount generation + parent directory inode + leaf component** rather than trusting the absolute path alone; direct root/fallback lookup still has a bounded path-selector, which is not a global Linux dcache. Mountpoint ancestor busy checks use the immutable VFS snapshot, not a backend's modifiable registration string. Host integration mutates a registration descriptor after attach, opens two independent aliases, detaches the later one without moving the earlier pinned mount, and reuses the hole for a nested mount without ABA. Real upstream ext4 verifies the internal VFS-owned mount is distinct from its adapter's registration token. Static architecture audit blocks regression to a compacting array. Only existing external providers are used; no vendor backend duplication.

### Writable lwext4 last-link orphan lifecycle (2026-10-28)

The original lwext4 `ext4_fremove()` truncates and frees a final-linked inode immediately, so VFS's previous blanket EBUSY for an already-open regular file was safe but not Linux-compatible. Writable ext4 now advertises **only regular-file** unlink-open safety. Before the last name is removed, the upstream adapter uses `ext4_frename()` to move the inode to a superblock-private, inaccessible `.naseed-orphans/o<inode>-<serial>` directory; this preserves the exact same ext4 inode number/data while generic File references remain live. VFS owns a mount-generation-aware inode lease lookup and calls the backend's `last_inode_unpin` only after the final generic lease is dropped; the adapter then invokes the unchanged upstream `ext4_fremove()`. `getattr` on the orphaned File reports nlink=0, and a fresh file may be created under the old name without aliasing the live orphan. Private records are filtered from root lookup/readdir and cannot be targeted by user VFS namespace mutations. A writable mount recovers leftover records from an interrupted shutdown before exposing its mount and performs a physical flush; corrupt or foreign private entries fail closed. All code remains in NaSeed's port and shared VFS, without modifying upstream lwext4/BlockDevice/ISA backends.

This is a staged, finite-capacity orphan protocol, not upstream ext4's native orphan inode journal: power-loss transitions between the vendor's multi-step rename and disk writeback still require ordinary filesystem recovery/e2fsck. Directory rmdir while open continues EBUSY; replacing an open destination via rename remains guarded until its independently atomic transaction is verified. The host and authentic ext4 writable acceptance now exercises last unlink, fstat nlink=0, the same inode's continued positional reads/writes, recreation under the old name, two successive final-close operations, and private-name invisibility. Keep this distinction explicit.

### ext4 rename-overwrite and open destination (2026-10-28)

The pinned original lwext4 `ext4_frename()` calls `ext4_create_hardlink(...)`, which returns EEXIST when destination exists: **it never provided Linux replacement semantics**, even for an unpinned destination. The NaSeed filesystem adapter now preflights source/destination inode identities and file/directory types, stages the *victim* into its hidden, per-superblock orphan namespace, executes the upstream source rename, and on failure attempts to restore the original destination path before returning an error. A replaced victim whose final ordinary name is gone but has an open generic File remains backed by the same ext4 inode until last VFS unpin, with fstat nlink=0; an unowned victim is reaped after successful replacement. Same-inode hardlink aliases are no-ops; mismatched directory types reject before mutation. Regression uses actual ext4 data for pinned target replacement and unpinned replacement across the same writable image.

The multi-call adapter transaction is **not power-failure atomic** and not a claim of POSIX crash consistency or full native ext4 journal orphan semantics. Rename-overwrite of directories stays conservative (EEXIST) until nonrecursive directory-specific transaction and lifecycle are verified. This is a filesystem capability implementation, not duplicated VFS namei or an upstream code modification.

### Actual ext4 crash-orphan recovery evidence

The original ext4 host integration now creates and writes a genuine file, then deliberately calls the *unchanged* vendor ext4_frename() behind the VFS to simulate loss of in-memory File/orphan metadata while the on-disk orphan name remains fully linked. A fresh read-only mount verifies those bytes still exist on disk without exposing the private path through VFS. The subsequent independent writable traced-driver mount must remove the exact orphan name before the filesystem becomes visible; e2fsck validates disk consistency. Recovery validates that each hidden name's encoded inode number matches its actual directory-entry inode, rejecting foreign entries instead of deleting arbitrary similarly named files.

### 2026-10-28: one typed ext4 orphan transaction for files and empty directories

The original vendor ext4_dir_rm() can delete a non-empty subtree, while ext4_frename()/ext4_dir_mv() refuse occupied replacement targets. The NaSeed ext4 adapter now uses one typed private-orphan staging path for both regular files and **EMPTY directory** replacement, with an explicit emptiness guard before EVERY ext4_dir_rm invocation. A failed source move rolls the original destination back from the staged name; an opened victim directory retains its physical inode until the final VFS owner releases it, and post-crash mounted recovery reaps only strictly validated private file or directory records. Original vendored ext4 remains byte-for-byte untouched. The shared VFS rename preflight rejects moving a directory beneath itself based on canonical component boundaries. Host real-image acceptance checks nonempty replacement ENOTEMPTY, valid directory subtree replacement, pinned directory victim lifecycle, RENAME_NOREPLACE, and recovery of BOTH file and directory private names across RO and RW remounts. These multi-operation adapter sequences are **not power-failure atomic**; no journaling/transaction durability is claimed. This is stronger POSIX runtime semantics, not a universal Linux dcache.

### Open-directory rmdir lifecycle (2026-10-28)

The common VFS now has a per-kind opened-directory unlink capability in addition to the existing file capability. For a pinned EMPTY ext4 directory, rmdir moves its last visible name into the strictly private typed orphan tree, preserving the actual inode and readdir/stat on its open File until final unpin. Re-creating its visible name allocates a distinct inode; final close removes the private directory without any recursive deletion of children. A NONEMPTY directory returns ENOTEMPTY even with an owner. Generic VFS still refuses this operation for providers that have not opted into safe orphan lifetimes. The original lwext4 sources remain unmodified. Openat/getcwd from an unlinked directory are not claimed to be fully Linux equivalent; the adapter rejects stale namespace reconstruction rather than reopening an unrelated path.

### Positive Dentry name index and non-recursive lifetime (2026-10-28)

The first counted Dentry/Path implementation used one global live-owner list
for both destruction and positive lookup. Each new independent directory
File or mount attachment scanned EVERY live Dentry, making a large collection
of distinct (mount, parent, name) owners cost quadratic work in the pathname
cache even though there are no negative dentries. This is not a good
foundation for future multiple independent filesystems or longer running
Linux workloads. It also used recursive parent Dentry destruction, which
needlessly consumed one kernel C stack frame per released ancestor.

A separate, fixed 257-bucket positive-name index now keys live entries by
(parent Dentry pointer, basename, mount incarnation). The existing live
list remains solely for lifetime inspection and namespace invalidation;
no backend or Linux syscall owns the index. Every acquire/pin first probes
the index. A detached Dentry immediately leaves the index, but retains its
inode, parent and mount references until its own final File/Path put. Same
mount and name with a replaced inode detaches the former object; an upper
mount root and its covered lower inode remain distinct because their mount
incarnations differ. Rename rekeys the SAME owned Dentry without allocating
or reconstructing any provider pathname after the backend operation.

Final Dentry put is now iterative: the teardown removes its positive hash
entry (if still visible), drops the owned inode, and releases parent links
upward only when their counts reach zero. Even a deeper directory chain
cannot consume one recursive C call per component on the limited kernel
stack. No heap object remains in the index after its final put. Like the
existing VFS, the table is single-CPU only; future SMP needs publication
and reclamation synchronization of BOTH owner list and hash buckets.

The original common VFS host fixture now pins 80 independent mounted
positive Paths simultaneously, repeatedly rebinds and copies their owner
references across all mount chunks, checks exact Dentry identity and
canonical names, then closes them in reverse order before mount-hole
recycling. The original vendor RamFS/LittleFS/lwext4, unmodified Linux
programs, and three-ISA QEMU acceptance stay mandatory; a green host
fixture alone is not a claim of completed SMP/dcache or all-head CI.
