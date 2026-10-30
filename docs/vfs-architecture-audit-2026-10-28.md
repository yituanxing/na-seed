# NaSeed VFS architecture audit — 2026-10-28

Scope: actual VFS implementation at PR #146, real original Debian
BusyBox and original RamFS/lwext4/LittleFS integrations, not a new
prototype. Linux's inode/dentry/file/super_block/mount and NaOS's
mount/provider model are conceptual comparators, not a reason to copy
implementation-specific internals into the syscall layer.

## Functional layering (now substantially correct)

`Linux ABI (int dirfd, O_*) -> common FS namei -> VFS mount/inode/File ->
provider (one-name inode callbacks) -> optional block device`.

- **Good:** `kernel/fs/lookup.c` owns symlink walk and dot/dotdot;
  `kernel/fs/vfs.c` owns mount dispatch, open inode lease, cross-mount
  EXDEV, lifecycle policy and unified VFS methods.
- **Good:** `kernel/fd.c` owns shared open-file descriptions and
  duplication/fork lifetime. `kernel/fs/metadata_syscall.c` deals with
  Linux stat/dirent64, without exposing those layouts to providers.
- **Good:** `kernel/fs/vfs.h` accepts kernel inode identities and child
  basenames. Original jkent/ramfs, lwext4, LittleFS and own tmpfs
  adapters exercise genuinely separate backing stores.
- **Proved via original application, not assumption:** unmodified
  Debian BusyBox 1.37.0 across 3 native QEMU ISAs found the *at ABI
  zero-extension bug and all three ISA applet workloads subsequently
  passed after one shared ABI repair.

## P0 issues addressed in this audit batch

**Fixed shared inode lease capacity:** `kernel/fs/vfs.c` had one 256
entry global `inode_size_lease` array. 320 genuine RamFS files were
previously checked sequentially, so none revealed the maximum 256
simultaneously pinned inode objects. Every inode lease also holds
POSIX record-lock state, which MUST NOT move when a new inode is pinned.
Implemented 32-entry address-stable, reusable heap chunks using the
existing common `nas_heap_calloc`; no FS-specific new object.
No change to an existing File/inode lease pointer when capacity grows.
The same slots are reused after all owned references are released;
the pool retains high-watermark capacity rather than leaking one
allocation per file created.

**Tests:** host VFS pins 512 distinct inode identities simultaneously
and exercises opening/truncating under live-lease pressure; original
unchanged RamFS host integration pins 320 actual vendor entries at
once and validates metadata. Kernel heap linked into all VFS host
test fixtures (bootfs/tmpfs/procfs, LittleFS, lwext4). This exposes
a real resource-limit architectural failure, not simply a renamed file
or copied code. Allocation failure returns ENOMEM; O_CREAT checks
room before creating its new pathname.

## Remaining architecture risk by priority

1. **P0 — mount/dentry ownership consistency.** Stable `mount_slot`
   anchors parent mount/inode, covered inode and their leases, but
   `nas_vfs_lookup` selects by longest absolute pathname prefix,
   `nas_vfs_lookup_step` uses anchored inode identity, and
   `nas_vfs_directory_path` must reverse map a live inode to a
   pathname to support dirfd/cwd. The code explicitly calls this an
   *interim* mount selector. We should ultimately make mounted
   dentries first-class objects and keep inode lookup independent
   of changing directory names. Do NOT bypass the real provider by
   adding a special RamFS/external app syscall path.
2. **P1 — tmpfs bootfs dependency.** The bootstrap `/tmp` root is
   a borrowed CPIO inode; `fallback_on_miss` maintains a legacy
   archive overlay, while RamFS/lwext4 are independent mounts.
   The next stage should define explicit overlay/underlay semantics
   at the VFS mount layer, not keep teaching every general FS
   operation about bootfs inode pointer equality.
3. **P1 — provider rename capability contract.** The original
   third-party RamFS does not guarantee atomic replace-over-existing
   open inode. Its adapter must continue to report EOPNOTSUPP
   rather than violate POSIX or risk an already open destination.
   Grow real BusyBox `mv` (overwrite same mount) and SQLite/Git
   atomic replace probes to drive a generic contract, not silently
   weaken the test or patch the source to fit NaSeed.
4. **P1 — bounded ownership and pathname tables.** Mounts now allocate stable 16-entry chunks up to 256 attachments;
   flock still limits 128 owners and POSIX locks 16 per inode. All path buffers are 256 bytes. The inode
   lease pool has been decoupled from these separate limits. The
   pathname limit is a clear gap from Linux PATH_MAX but should
   not be 'fixed' by enlarging every stack buffer unreviewed.
5. **P2 — scheduling and concurrency.** VFS currently assumes a
   single CPU; dynamic lease pool and mount publication are not
   independently synchronized. When true SMP/file concurrency
   arrives, locking, allocation contexts, reference count atomicity
   and inode cache ownership must be designed together rather
   than sprinkling locks over callbacks.
6. **P2 — production test breadth.** The current BusyBox matrix
   exercises tmpfs and original RamFS in one real guest; littlefs
   and lwext4 already have separate real integrations but do not
   yet run the same comprehensive byte-exact multi-filesystem app
   profile with persistence verification after remount. That
   difference is tracked, not inferred as pass.

## Recommended validation model

Keep the Linux host oracle byte-exact for the unmodified original
program. Run the exact BusyBox applet sequence against at least two
genuinely independent provider implementations in each native ISA.
On the first failure, locate the responsible owner using actual
stack/syscall/error evidence. Apply one shared VFS/ABI correction
for all backends before touching per-provider adapters; only then
run focused CI and a final grouped full regression. The original
vendor source is a controlled independent variable, not something
to silently rewrite until green.

This is an architecture assessment plus targeted implementation,
not a claim that a fully Linux-equivalent VFS or dentry cache already
exists.

## Explicit bootstrap-only virtual mountpoints (2026-10-28)

The previous shared mount registry could silently attach a filesystem over a
nonexistent directory. Ordinary mount registration now requires an existing
directory at its final mountpoint and refuses missing root or covered names
with ENOENT. Internal early-boot registrations explicitly opt into
`allow_missing_mountpoint` for /tmp, /proc, /mnt, /flash and /ramfs; they
still cannot invent missing intermediate ancestors. This is not a public
Linux mount(2) implementation, and synthetic names do not yet appear in
the parent's getdents64 (an owned dentry/mount-tree gap). Re-registering
the same live token with a different pathname is rejected, preserving
unambiguous VFS-owned mount/unmount identity.

The common host VFS acceptance now exercises missing-target rejection,
explicit virtual bootstrap lookup/unmount, and duplicate token rejection
after registration descriptor mutation. Vendor source remains unchanged.

## VFS-owned readdir mount overlay (2026-10-28)

Parent readdir now observes mounted child dentries through the common VFS
layer, not through bootfs/tmpfs/ext4/RamFS/LittleFS special cases.
- For an existing covered directory, VFS keeps the provider's name/cookie
  and reports the mounted provider's real root inode (not the hidden inode).
- For an explicitly virtual early-boot mountpoint, the VFS enumerator inserts
  its name after dot/dotdot, before provider children. It maps logical
  cursors back to provider cursors without scanning all underlying entries;
  the same code works for all independently mounted providers.
- Attachment identity uses the live parent (mount generation, inode number,
  opaque backend inode) rather than matching stale absolute path text.

Host tests exercise whole-buffer and split-buffer cursors, mounted inode
identity, detach and return to the original namespace. The virtual boot
entry is now readdir-visible, but this is still NOT a fully cached dentry
tree or Linux mount namespace: directory mutation during an enumeration
can change logical cookies, and further work is needed for per-File
struct path and mount topology lifetime. Original upstream code is unchanged.

## Stale mount-parent protection and strict directory records (2026-10-28)

The older direct `nas_vfs_lookup` lower-layer fallback still selected
attachments by absolute longest prefix alone. It now verifies a live parent
inode identity (mount generation, inode number and backend incarnation)
against the VFS-owned mount anchor, just like the component namei path.
If the original parent pathname refers to a different inode, the stale
child attachment cannot be selected. A host regression deliberately
changes a mock parent-root inode while a nested child is mounted; direct
lookup and component namei both reject it until the provider identity is
restored. No per-filesystem mount routing exists.

The common readdir boundary now validates provider-generated names before
calling `strcmp` to find a covered child, and verifies the dot/dotdot
prefix before synthetic entries. Previously a malformed backend record
could be overread or have its corruption masked by replacement with
mounted inode metadata. All providers share the same strict VFS gate.

The three historical external compatibility workflows (Lua, original
TinyCC 0.9.27, and RV64 UEFI RTC A/B) are pinned to the same Ubuntu 24.04
runner family as the already-green newer native TinyCC matrix. This is
environment-variable isolation, not proof of a kernel fix or a relaxation
of guest pass markers. Run results must be checked on this exact head.

## Stable mount registry growth and lifetime (2026-10-28)

The static 16-mount table was a hard scalability failure independent of the
inode lease table. Replacing it with a reallocating array would have been
incorrect: open File, cwd, mount-parent Dentry and borrowed inode snapshots
refer to published superblock addresses. The VFS now keeps its initial
16 boot slots in static storage (no heap required during early boot), grows
in zeroed, address-stable 16-slot heap chunks only when all slots are in
use, and retains those chunks for hole reuse. A total of 256 slots remains
an explicit resource limit; this is not an unbounded mount namespace.
A new mount is published only after valid path and owner checks; failure
to allocate an extra chunk returns ENOMEM, not a partially attached mount.
A newly attached mount receives a fresh device incarnation and a stale
borrowed inode cannot access that slot even when its address is reused.

The real host VFS/provider fixture holds 48 simultaneous independently
mounted root directories, verifies their live lookup and VFS-only readdir
entries in split/cursor calls, pins an early mount across chunk expansion,
attaches/detaches a child beneath a late heap-backed slot, and recycles
holes across both storage tiers while checking old incarnation rejection.
No CPIO, RamFS, ext4, LittleFS, U-Boot or real vendor source is modified.
This test does NOT prove SMP safety, physical device hotplug, mount
namespaces, or correct behavior after exhausting the kernel heap.
