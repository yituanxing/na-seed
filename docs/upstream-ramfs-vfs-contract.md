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

- Upstream `ramfs_unlink/rmdir` immediately free inode memory.
  The adapter cannot promise Linux unlink-open lifetime without modifying
  upstream or adding an independent orphan owner. Hence we return
  `EBUSY` on pinned inode and keep `unlink_open_safe=false`.
  This is a *deliberate fail-closed mismatch*, not full POSIX semantics.
- Upstream rename rejects existing destinations; NaSeed refuses
  rename-overwrite with `EOPNOTSUPP` instead of deleting victim first
  without an atomic transaction. Never silently substitute a tmpfs-only
  special-case for a missing shared VFS capability.
- This embedded RAMFS does not implement symlinks, hardlinks, POSIX
  timestamp/mode mutation, ACLs, mmap or Swap.
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
