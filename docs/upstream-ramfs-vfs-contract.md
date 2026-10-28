# Independent upstream RAMFS as a real NaSeed VFS contract probe

## Provenance and upstream-code rule

Source: `jkent/ramfs` at immutable upstream commit
`0e8b9bcd94e1efecf7260c38b7dc37f5a09ebc3b`. The **vector**
backend was selected, not its optional rbtree backend. Unmodified files:

- `third_party/jkent_ramfs/src/ramfs_vector.c`: Git blob `4b5728b58bf6024f0396ba1626c03a338b7eba1f`.
- `third_party/jkent_ramfs/include/ramfs/ramfs.h`: `b63df2285eb32cbe39554331e2c535cb0717c79a`.
- `third_party/jkent_ramfs/LICENSE.txt`: `be2cc4dfb609fb6c38f6365ec345bded3350dd63`.

Upstream is **MPL-2.0**, not BSD/GPL; original notices and full
license are preserved and the port is a separate set of NaSeed files.
MPL 2.0 Section 3.3 permits secondary GPL distribution of covered
source in a larger GPL work **if** the work is not marked Incompatible
with Secondary Licenses, subject to the dual-availability obligations.
Do not strip license headers or relicense without meeting those terms.
This is a license review item before release, not a claim of legal sign-off.

## Why import instead of only studying another OS's tmpfs

Linux ramfs uses existing Linux page cache and dcache; directly lifting
its code would bring those exact dependencies. This independently
packaged upstream C library has its OWN in-memory inode/directory/data
model, so importing it exposes the *real* NaSeed VFS contract.
The upstream backend is deliberately never patched to accommodate NaSeed.
Only the freestanding C compatibility shim and VFS adapter are ours.

Shared interface: VFS owns mount records, component namei, borrowed/owned
inode leases, syscall ABI, File offsets and mount lifetime. The external
backend owns its internal entries, tree and data buffers; an adapter keeps
mount-local stable inode identity and parent/child relationships.
The same opt-in guest implementation is linked on x86-64, RV64, AArch64.
Mount point is `/ramfs`; the existing `/tmp` tmpfs remains unchanged.

## Real coverage and limitations

Implemented through actual shared VFS:
mount/unmount, root+child by parent inode, create, mkdir, getdents64,
read/write at offsets, truncate (including zero), getattr, inode identity
validation, rename without replacement, parent-rename-aware directory
FD path, unlink and rmdir, busy-mount and inode lease protection.

**Known true interface gaps** surfaced by this upstream implementation:

- Upstream `ramfs_unlink/rmdir` immediately free inode memory, but the
  VFS adapter now supplies an independent **orphan owner**: it moves
  pinned deleted files/empty directories to an unreachable private
  backend directory, retains the same inode identity and data and
  invokes the original vendor unlink only at VFS final inode release.
  The public name can be reused without aliasing the old File. This
  uses the existing `last_inode_unpin` interface; upstream is untouched.
  The private directory is excluded from public namei and readdir.
- Upstream rename rejects existing destinations; NaSeed refuses
  rename-overwrite with `EOPNOTSUPP` instead of deleting victim first
  without an atomic transaction. Never silently substitute a tmpfs-only
  special-case for a missing shared VFS capability.
- This embedded RAMFS does not implement symlinks, hardlinks, POSIX
  timestamp mutation, ACLs, mmap or Swap. The adapter retains mode
  metadata per VFS inode and supports `chmod` without vendor changes.
- The embedded library uses a heap and its *own* in-memory data storage.
  Do not conflate it with Linux ramfs's reliance on generic page cache.
- The upstream project may have bugs/limitations; this is a distinct
  acceptance control, not proof the imported backend is production-grade.

## Regression contract

`sh scripts/check-upstream-ramfs-vfs.sh` compares pinned original
Git blob hashes, executes the real compiled VFS host with **two independent
mounts** and common namespace operations, verifies rename with held inode
and directory File, open-unlink busy behavior and unmount refusal.
It also builds the same original source+port in three freestanding ISAs.
`NAS_ENABLE_RAMFS=1` links the full native kernel and runs the VFS
selftest on boot (when a real QEMU boot gate executes).

The imported independent FS is not a replacement for NaSeed tmpfs or
lwext4/littlefs. It is a *control variable*: if one VFS contract cannot
support both independent implementations, record the mismatch and fix
the owner, instead of copying old per-filesystem dispatch logic.

### Upstream portability quirk (preserve source)

The upstream `ramfs_vector.c` uses `errno` without including its own
`errno.h`. Both the hosted and freestanding import compile with
`-include errno.h` rather than changing a single byte of the original
MPL-2.0 source. The host defines its native errno, while the kernel
port exposes a separate storage slot for the vendor library.

### Common heap becomes a shared kernel owner

Importing *two* independent upstream libraries exposed a real structural
problem: the lwext4 guest object had privately compiled
`kernel/mm/heap.c`, while RamFS also needs exactly the same heap.
The correct boundary is ONE common NaSeed kernel heap compiled from
`scripts/full-kernel-core.sources` and linked into all three ISAs.
lwext4 kernel mode now imports common `nas_heap_*` symbols; only its
standalone/closed host linker test embeds the allocator. RamFS uses the
very same symbols. Never condition an allocator's ownership on which
filesystem happens to be enabled first; never build duplicate allocators
with separate arenas. The original external vendor source remains
byte-identical.

### Audit rule: no duplicated numeric common-core count

The original checks maintained independent hardcoded TU counts in both
`check-three-isa-core.py` and `check-full-kernel-sources.sh`. A legitimate
shared allocator addition caused the latter to reject 64 common TUs while
an adjacent Python audit accepted 64. Both now derive the count only from
the versioned manifest, validate no duplicates and exact common x86/RV/ARM
link coverage; the Python audit also requires the VFS and common allocator
owners. Adding a common service no longer requires patching several unrelated
hardcoded magic counts.

### 2026-10-28 real third-party stress: inode generation and metadata ownership

This VFS integration exposed another first-party architecture defect: the
original adapter reserved exactly 128 inode identities in a mount-wide
static table, despite the external filesystem itself supporting larger
directories. It also silently discarded creation modes. The adapter now
allocates identity objects on demand through the one shared kernel heap,
recycles inactive records with a **new** monotonically increasing inode
number, validates identity provenance without UB pointer-range comparisons,
and keeps mode metadata in its own per-mount sidecar (including chmod).
Vendor bytes remain untouched. On failure to allocate the sidecar for
create/mkdir, the adapter rolls back the just-created vendor inode.
Unmount returns all sidecar objects to the common heap after the last
VFS lease is dropped; no filesystem-specific process FD dispatch appears.

The compiled, real VFS host acceptance creates and enumerates **320**
records using one 7-entry getdents cursor (old hard cap: 128), validates
per-file creation modes, chmod, stale generation after delete/recreate,
then stresses **700** create/lookup/delete generations. The unchanged
native guest source remains used for all three ISA real QEMU acceptance.
This does *not* solve the upstream vendor's immediate-free open-unlink or
atomic rename replacement limitations; those remain explicitly fail-closed.

### 2026-10-28 interface closure: original vendor open-unlink lifetime

The previously documented `EBUSY` for an opened unlinked RamFS file
was a real limitation of the thin adapter, **not** an unavoidable VFS
limitation. We now reuse the generic VFS `last_inode_unpin` hook, also
used by tmpfs/lwext4, to implement the missing lifetime *in the port*:

1. VFS owns an inode lease on an independently opened File or directory.
2. Unlink of a pinned inode moves the still-live vendor entry to a
   mount-local `/.nas_ramfs_orphans/o<16-hex-inode-generation>`.
   No user lookup, creation, rename, or readdir can observe that name.
3. The unlinked File continues reading/writing by the SAME vendor
   `ramfs_entry_t*`, without pathname reopening or byte copying.
   `getattr` returns link count zero.
4. The public name may be recreated while the old File remains open.
   On the final generic VFS unpin, adapter callback invokes the original
   vendor unlink/rmdir, recycles the sidecar generation, and frees data.
5. An open empty directory follows the same deferred-reclaim protocol;
   nonempty directories are rejected *before* any internal rename.

All original third-party `ramfs_vector.c` and headers remain byte
identical to upstream. Provider explicitly sets
`unlink_open_file_safe=true` and `unlink_open_dir_safe=true`; no
filesystem-specific kernel syscall or VFS File variant was introduced.
Rename-overwrite remains `EOPNOTSUPP` because upstream does not provide
a safe atomic replacement. Hidden-orphan recovery after kernel panic
is irrelevant to this nonpersistent RAM filesystem and is not a claim of
crash durability.

This same batch changes the original vector-backend directory scanner
to use its upstream O(1) `ramfs_seekdir` for ordinary subdirectories;
the mount root filters its private namespace and uses a visible-only
logical cursor. Actual host VFS and all three native QEMU guest tests
must prove this behavior before considering it accepted.

### Genuine three-ISA user/syscall ABI acceptance (not only kernel selftests)

The last acceptance exercised RamFS via kernel-owned VFS calls; it did
not establish that Linux userland could interact with the independent
backend. Now `user/ramfs-native-vfs.c` uses one unprivileged ELF source
on x86-64/RV64/AArch64 with the existing common, ISA-specific
`ext4-rw-syscall.S` wrapper (only Linux `unlinkat` was appended to the
shared ABI tables, retaining existing indexes). One generic target
matrix `scripts/build-native-vfs-probe.sh` compiles both the existing
LittleFS probe and the new RamFS probe without duplicating triple,
ABI or linker selection.

The genuine Linux syscall route runs openat/O_CREAT|O_EXCL, concurrent
independent File descriptions, lseek/live growth, read/write/fsync,
rename of open inodes, unlink-open with new inode at the same name,
getdents64 excluding the private orphan store, mkdirat, dirfd-relative
openat/unlinkat, fchdir/getcwd through renamed parent, and close/final
orphan reclaim. All operations enter the actual native Linux syscall
dispatcher, NOT the kernel-side RamFS selftest.
The three ISA native QEMU workflow checks distinct ring3 and
kernel-side markers, exit 0 and clean unmount. No filesystem-specific
FD type, user ABI or syscall dispatch branch was introduced.

A deleted-but-open directory remains valid for inode metadata/readdir,
but its *namespace pathname* does not exist. The adapter must return
ENOENT from `directory_path` instead of allowing the internal orphan
spelling to escape via `fchdir/getcwd` or dirfd path reconstruction;
the host VFS fixture explicitly checks this boundary.
