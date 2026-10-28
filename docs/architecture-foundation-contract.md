# NaSeed architecture foundation contract — design before implementation

**Decision date:** 2026-10-28. **Source reviewed:** NaSeed PR #146 at
`275e9d86a91c2e36176e1e7ef8913b9d03491c50`, Linux upstream VFS
and memory-management documentation, and NaOS
`aether-os-studio/na-kernel@2c45649144afa3dd2e32147d7029e354dc9fc518`
(`kernel/src/fs/vfs/{vfs.h,vfs_path.c,vfs_mount.c}`,
`kernel/src/mm/{mm.h,vma.h}`, `kernel/src/block/{block.h,partition.h}`).
The Linux and NaOS object models are semantic references, not a license
to copy unreviewed code or invent interfaces before real providers use them.

## Architecture decision: keep real programs, change the order

For a local behavior bug, fix the owning provider or shared algorithm.
For a NEW object category or a cross-subsystem feature, do the following
before modifying the implementation:

1. Map the complete object graph and its failure/unwind paths across all
   three ISAs, including existing live user applications and vendored ports.
2. Compare the ownership model with Linux and NaOS, then specify the *smallest*
   reusable contract. Include which existing fields and functions will be
   deleted, not merely moved to another source file.
3. Check the design against **two independent providers**, one of which
   must be real upstream code where possible. Never tailor the VFS to the
   test mock or modify the vendor source to make the interface pass.
4. Implement a coherent batch; focused host+guest tests, then 3-ISA
   exact-head grouped CI. No passing status is inferred from a build or
   compilation-only check. Report unsupported semantics as errors.
5. Delete old parallel implementations and make ownership boundaries
   executable in `scripts/check-architecture-boundaries.py`. Every new
   guard must have a failing mutation fixture.

The new baseline MUST retain the source-tested COW fork, IRQ-preempted
native kernel continuations, three native ABIs, real third-party FS
adapters and VirtIO physical ext4 persistence. No simultaneous change
to all of VFS, MM and the scheduler without independent rollout gates.

## Current boundary map and real gaps

| Owner | Current implementation | Architectural problem | End-state owner |
|---|---|---|---|
| Linux ABI | `kernel/fs/*syscall.c`, `syscall/dispatch.c` | legacy char `/dev` & AF_UNIX tmpfs shortcuts | syscall adapts ONLY; VFS/device/socket own objects |
| VFS | `fs/vfs.c`, `lookup.c`, `fd.c` | borrowed inode snapshot, prefix locator, string reconstruction | refcounted Dentry + Path (Mount,Dentry), Superblock/Inode/File |
| Root/bootstrap | `boot_image.c`, `bootfs.c`, `init/main.c` | initial ELF selected as Limine CPIO/direct module; bootfs baked into some runtime paths | bootloader hands resources; runtime exec uses VFS inode independent of bootfs |
| MM | `mm/mm.c`, `vm.c`, `fault.c` | real anonymous COW, but file-private eager copies and fixed 128-slot VMA | owned file VMA backing and per-inode page cache before MAP_SHARED |
| Device | `drivers/core/bus.c`, `port/dma.c` | QEMU-specific discovery/identity DMA, no general IRQ/resource lifetime | Device/Driver/Resource owners, separate from BlockDevice |
| Block | `block/device.c`, `ports/uboot` | synchronous sector calls and whole-device ext4 images only | partition BlockDevice views plus request/completion owners |
| Socket | `fs/unix_socket.c` | bind/connect directly use `tmp_*` and boot archive | pathname socket inode via generic VFS; AF_INET via NetDevice/lwIP |
| Task | `task/{process,sched,signal}.c` | bounded single-CPU objects, nested signal-frame limit | preserve task/MM/Files/Sighand/FS ownership while extending capabilities |

**The boot-loader is not the authority on executable location.**
`nas_boot_prepare` may select PID1 from an actual Limine module, while
`nas_task_execve` must resolve all later ELF and shebang interpreters
through the VFS even on a boot without a CPIO archive. A fresh VFS inode
is a BORROWED namei result until it is pinned. Pin both executable and
optional dynamic interpreter for the entire staged MM transaction; release
them on *every* failure or after the new MM has finished constructing its
own pages. The old MM, FDs and peer threads remain untouched on failure.
This is implemented in the first batch with host VFS rootless lookup coverage.

### Target VFS ownership, not a type-name refactor

```text
Task.fs.root/cwd --- owns ---> Path { Mount ref, Dentry ref }
Task.files/FD ---> shared File { offset, flags, Path ref, Inode ref }
Mount { parent Mount, covered Dentry, root Dentry, Superblock }
Dentry { parent Dentry, basename, positive/negative state, Inode ref }
Inode { Superblock, backend identity, open/pin refs, page mapping }
Superblock { provider type, backing block/device, mount refs }
```

**Invariant V1**: a rename changes a Dentry edge but never changes an
opened File's identity or its MM/file backing. `getcwd` and dirfd
resolve from a counted Path, not reverse-engineer a string in each provider.

**Invariant V2**: mount crossing checks the `(parent Mount, covered Dentry)`
identity. Dentry and mount references outlive lookup, open, cwd, and any
nested child mount. Unmount returns EBUSY while any such reference exists.
Mountpoints attached above a filesystem are visible through common
getdents64, without asking the lower filesystem to fabricate entries.

**Invariant V3**: inode ownership is independent of names. Unlink/rename
while open retains readable/writable inode data until final release;
provider-specific atomicity constraints are explicit capabilities, not
pretend-success shortcuts. A stale reused inode or mount may NEVER be
silently revived through the same pointer.

**Migration sequence**: (a) build genuine positive+negative Dentry
identity/lifecycle under existing `nas_fs_node`, (b) wire Path ownership
into cwd/dirfd/File, (c) swap namei and mount crossing to Path and delete
`nas_vfs_directory_path` and prefix fallbacks, (d) remove bootfs/tmpfs
compatibility from runtime namespace and turn synthetic `/dev` into
devfs. Do not introduce a mere `struct path` holding a string; no
fake object that repackages the existing prefix registry.

### MM/FS shared contract (design frozen, not implemented here)

A VMA must own an `inode/address_space` reference and file offset for
file-backed mappings. A memory `Page` object represents resident physical
bytes, owner/refcount, dirty/writeback state, and optional file index.
Anonymous COW and private file COW use page ownership and per-MM PTEs,
not shared mutable VMAs. `MAP_SHARED` cannot be claimed until dirty
propagation, coherency with read/write/truncate, mmap-unmap and fsync
are tested against an independent real filesystem. Leave the current
eager `MAP_PRIVATE` contract explicit rather than simulating MAP_SHARED
using a private copy. File-page caching and reclaim are separate gates.

### Device/Block contract (design frozen, not implemented here)

```text
Platform discovery -> Bus Device -> Driver binding
                              -> {Resource: MMIO/PIO, IRQ, DMA}
                              -> class Device (BlockDevice/NetDevice/etc.)
Filesystem -> BlockDevice view (whole/partition)
           -> Request/Completion -> Driver -> Hardware
```

**Invariant D1**: only the device/driver owner manages MMIO/IRQ/DMA.
Filesystem code sees neither PCI/MMIO nor user pointers. Driver removal
must fail while any BlockDevice, mounted FS, request, or mapping holds it.

**Invariant D2**: MBR/GPT parsing creates bounded, checked, refcounted
partition views in the Block layer. Filesystems receive a BlockDevice
and NEVER parse partitions themselves. Invalid CRC, overlapping/overflow
LBAs and short reads fail closed. A partition cannot outlive its parent.

**Invariant D3**: request completion has exactly one owner; timeout,
cancel, device removal and hardware failure release DMA and queue
descriptors exactly once. Existing synchronous read/write/flush remains
a wrapper around this future interface, not a second implementation.
Do not pretend today's QEMU identity DMA or polled VirtIO supports
general PCIe/IOMMU or asynchronous IRQ completion.

### Cross-subsystem invariants

- No Linux syscall number, usercopy, task FD or filesystem inode in
  `kernel/block` or generic driver core.
- No `arch/linux_*`, Linux stat/dirent structs, Task/FD/user pointer
  access in filesystem provider callbacks.
- No `bootfs_mounted()` gate in runtime `execve`. The CPIO root must
  not determine whether independently mounted VFS ELF can run.
- MM owns page tables/faults/COW. VFS owns file lifetime, not PTE bits.
- No per-ISA fork/exec, VFS, BlockDevice or TCP/IP algorithm forks.
- Resource exhaustion must return an explicit error without publishing
  half-created object state. No faked errno/pass markers.
- All three native ISA QEMU and relevant host tests must pass on the
  **same exact HEAD** before merging PR #146.

## Executable first-batch gate

This first architecture-led change:
1. Removes the historical `bootfs_mounted` early return from execve.
2. Pins the executable + dynamic loader via generic VFS inode lease APIs
   across ELF and stack staging, with complete failure/unwind paths.
3. Extends actual VFS host namei checks for independent rootless mounted
   filesystem lookup and inode-pinned unmount lifetime.
4. Adds a self-tested preflight layering guard; never calls CPIO routines
   from runtime exec or user FS ABI to infer an executable source.

**Honest limits**: the normal PID1 boot still selects an actual Limine
module; this is not yet disk-root PID1 or an on-disk `/sbin/init` test.
The no-CPIO host namei fixture is not a native QEMU execve test.
`/tmp` still borrows a CPIO directory when available. AF_UNIX pathname
bind still uses tmpfs internals and MUST NOT be "fixed" by changing only
its prefix check; a real VFS socket inode contract is needed. The first
batch does not implement full Dentry/Path, device IRQ, partition, MAP_SHARED
or multiprocessor locking. All are acceptance criteria for later batches,
not present-tense feature claims.

## Required independent acceptance

- Real CPIO/no-CPIO namei; real ext4, original RamFS and LittleFS host
  integration; real QEMU user ELF and failures (ENOENT/ENOEXEC/ENOMEM).
- Real BusyBox openat/chdir/rename/getdents64, opened-then-renamed dirfd,
  unlink-open inode, nested mount and detach/ABA.
- ext4: real VirtIO PCI/MMIO on three ISAs; fsync + clean unmount and
  disk bytes verified across TWO separate QEMU boots and host e2fsck.
- VM: fork COW, failed exec rollback, ELF + dynamic interpreter, mapped
  file writes and concurrent readers; later true MAP_SHARED coherency.
- Drivers: at least RAMDisk and original U-Boot VirtIO-Blk; later
  independent partition driver and non-block driver share one Bus ABI.
- Every architecture-boundary rule must have a negative self-test, and
  old parallel code must be deleted when its owned replacement lands.

**Rollout rule:** no unverified cross-subsystem "big bang". Review the
entire dependency graph once; implement multiple *related* changes in one
batch, compare exact HEAD and refuse merge on any failed acceptance.

## Real boot/runtime separation acceptance — original program

`scripts/probe-vfs-exec-no-cpio-x86.sh` is a separate **native x86-64
QEMU gate**, not a host mock. It boots the existing VirtIO-PCI/lwext4
kernel with a pinned original Debian BusyBox as a direct Limine ELF
module, **without any CPIO initramfs**. It places the exact same static
BusyBox ELF on a real independently attached ext4 raw drive, then uses
the initial BusyBox shell to `execve("/mnt/busybox")`. The guest must
log VFS provenance `lwext4 ELF`, exact stdout/exit zero and physical
VirtIO device discovery; the host compares both complete ELF byte
streams before/after boot, checks clean e2fsck and rejects any CPIO
mount marker. The test is invoked after the physical x86 VirtIO kernel
has been built, from the existing lwext4 acceptance workflow.
This gate checks the structural decoupling without claiming the full
disk-root PID1, pivot_root or a generic Linux mount(2) interface.

The manifest `scripts/vfs-rootless-exec-cases.tsv` preserves all original
Linux guest pass checks, and its direct-module mode is deliberately
different from the existing CPIO BusyBox test suites.

## First actual partition BlockDevice — contract and integration

`kernel/block/partition.[ch]` probes MBR primary entries and protective
MBR / GPT using header and entry-array CRC32, bounds, usable LBAs and
partition type GUID. Extended-MBR chains, hybrid formats and recovery
from the secondary GPT header are deliberately **not supported**.

`nas_partition_view` is a registered child BlockDevice holding a stable
parent BlockDevice pin until final detach. It translates checked I/O
offsets and forwards write, RO and flush semantics; neither VFS nor a
filesystem knows the partition start sector. Physical-parent removal
fails EBUSY while an attached child view exists.

The same code is built on all three ISAs. Host tests validate MBR, GPT
with 512/4096 logical sectors, malformed CRCs, bounds, parent lifetime,
and an independent upstream-style block-driver adapter. A pinned
unaltered lwext4 filesystem is then mounted through a *real partition
view at nonzero LBA*, verified by VFS inode reads and busy unmount.
Physical GPT-backed QEMU execution is a **separate future gate**;
host success must not be described as such.

## Real physical GPT QEMU acceptance

The generic partition view is now used by the **same real native boot
path** as whole-disk ext4. `ports/lwext4/guest_boot.c` discovers
physical VirtIO BlockDevices, detects whole-device ext4 first, and
otherwise probes MBR/GPT ranges through `nas_partition_probe`. For a
candidate ext4 partition it **registers an owned BlockDevice child**
before passing its stable name to the unmodified lwext4 adapter. Clean
unmount releases upstream ext4 and VFS before detaching the partition,
which then releases the parent hardware device.

`scripts/probe-vfs-gpt-busybox-x86.sh` uses real `sgdisk` to generate
a 64MiB GPT physical disk, places the original ext4 filesystem image
inside its first 40MiB partition at LBA 2048, and boots the unchanged
original Debian BusyBox workload twice through genuine x86-64 QEMU
VirtIO-PCI, checking guest mount provenance, disk bytes, GPT verification
and e2fsck. This is an **additional** test; whole-disk and RamDisk
acceptance remain unchanged. Native RV64/AArch64 link the same partition
implementation but physical GPT QEMU on those ISAs is not claimed here.

## Same physical GPT executable gate on all three native ISAs

The original physical-GPT x86 acceptance has been generalized to
`scripts/probe-vfs-gpt-busybox.sh`, driven by the same three native
Debian BusyBox parity harnesses already used by the whole-disk driver
acceptance. The script constructs an independently GPT-formatted
VirtIO disk for each ISA, places the original ext4 filesystem at LBA2048
inside a real partition, and requires two independent QEMU boots
sharing that disk with genuine original BusyBox workloads. Verification
uses original guest exit/provenance, host GPT CRC verification,
partition-offset extraction, e2fsck, and persistent inode bytes.
The previous `-x86.sh` entrypoint delegates to the single cross-ISA
gate rather than retaining a separate implementation.

## First genuine positive-Dentry and owned-Path migration (2026-10-28)

The VFS now owns a positive Dentry graph keyed by parent Dentry and
basename, and validated by the exact mount/inode incarnation. A Dentry
holds a counted kernel VFS inode and mount reference and a parent Dentry
reference. Objects disappear when no File, FS context or child holds
them; there is no unbounded string-based path lookup cache.
`struct nas_vfs_path` carries explicit (mount, mount_id, Dentry) identity
with get/put/string lifecycle, not an absolute-path wrapper.

Directory `File` descriptions bind their owned Dentry BEFORE publishing
the FD. dup/fork share that File, releasing the Path on final close. The
FS context holds a Path with forked reference-counted ownership and
CLONE_FS sharing. getcwd, openat(dirfd), fchdir and task-relative namei
use Dentry parent links rather than provider-specific reverse path
traversal, when an owned Path exists. Each chdir/fchdir binds a Dentry to
the exact VFS inode instead of trusting a stale pathname hint.

VFS rename and unlink now acquire parent Path owners before invoking
the provider. After success they update a Dentry's parent/name linkage
without further filesystem lookups or fallible allocation; open child
directory Paths automatically follow a renamed ancestor. An unlinked
but still-open directory remains a pinned inode, but its Dentry is
detached and cannot supply a new namespace path. Generic VFS owns this
logic; no lwext4, RamFS or tmpfs-specific Dentry mutation callback.

**Limitations:** This is an incremental but functional migration. The
old inode-based VFS mount selector, `cwd_node`, `origin_path`,
`nas_vfs_directory_path` and provider `directory_path` callbacks are
retained for existing callers without owned Paths; they are not yet
deleted. Full negative dentries, mount movement, Linux open_tree APIs,
SMP synchronization and removal of all legacy path recovery remain
separate and must not be claimed as implemented. Do not merge without
the exact-head native 3-ISA QEMU and real third-party filesystem tests.

**Independent acceptance:** The original unmodified upstream ext4 test
now owns nested/duplicated directory Paths across real rename, verifies
directory ancestor movement, reuse of the old name by a distinct inode,
and rejection of Path string construction after unlink while references
remain. A second test runs no-CPIO rootless mounting, Path ref sharing
and busy-unmount without a fabricated '/' inode.

## Mount attachment identity: first counted Path owner

The Mount attachment now owns `parent_path` and (when a covered inode
exists) `covered_path`, using the **same positive Dentry graph** as
opened directory Files and FS contexts. Mount registration obtains
these references before publishing its stable slot; failures roll back
the prepared Dentry owner without publishing a half-attached mount.
Unmount releases covered and then parent Path exactly once. There is
no parallel `parent_pinned`/`covered_pinned` inode-only owner anymore;
mount-owned Paths provide the existing inode/mount busy protection.

The direct longest-prefix bootstrap selector remains an explicit,
bounded compatibility route. It first reconstructs the attached
parent from an OWNED Path, validates that it is still at the expected
name and revalidates the live provider inode incarnation before
selecting a mount. The latter is necessary for a real backend that
recycles a parent-root inode without VFS mutation notifications.
Normal component lookup already matches the mounted (parent
mount-incarnation, parent inode, basename) identity. Genuine
mount movement, root mount namespace changes and the complete removal
of this prefix selector remain separate migration gates.

Test gates include existing real mount parent pin, parent inode recycle,
nested mount EBUSY, refcounted rootless mount, mounted readdir and
original external lwext4/RamFS on all native ISAs. The Task/MM isolated
test has explicit fail-closed linkage for Path methods because it never
constructs any Dentry; production VFS code is still used by genuine
filesystems and QEMU, without pretending the Task fixture tests VFS.

## Directory File open transaction belongs entirely to VFS

`nas_vfs_open_inode()` now returns the counted backend inode AND optional
positive directory Path in one transaction. The VFS itself binds the
Dentry after validating/pinning the exact inode and before truncation
or File publication. The Linux syscall adapter no longer constructs
a Path from inode and string; it only transfers VFS-created references
into the File description. Host tests directly verify successful
directory owner transfer and independent last-put behavior. An
inode-only caller may explicitly pass `NULL`, retaining the one
authoritative VFS algorithm without a second parallel open API.

## Remove normal dirfd/fchdir reverse path fallback

Normal `openat(dirfd)` now requires the Path returned by its *single*
VFS directory-open transaction; if an external/synthetic File claims
to hold a directory inode but has no owned Path, the ABI fails closed
with ESTALE instead of re-opening its saved origin string.

`fchdir` now directly acquires a ref to the File's existing Dentry
with `nas_vfs_path_get`, then transfers it transactionally into the
FS cwd context alongside its own inode lease. It never invokes provider
`directory_path` or reconstructs a new Dentry by namei. `chdir` still
binds a resolved canonical path exactly once. Existing `getcwd`
compatibility fallback is retained only for historical inode-only
FS-context boot fixtures; it is NOT used in normal Path-owned flows.

## Covered Dentry vs mounted root — distinct mount layers

The positive Dentry hash key MUST distinguish the *mount layer*.
The exact same parent/name can simultaneously identify a covered inode
in the lower mount and a visible root inode in the upper mount.
Only a same-mount new inode incarnation detaches the old Dentry as
stale name reuse. Acquiring a Path to a mounted root must NEVER
detach the lower mount's covered Path. A real VFS host test now
retains lower and upper Paths simultaneously over one mount,
proves both valid, verifies distinct mount identities and blocks
unmount while the upper owned Path exists. Vendor filesystem code
is unchanged.

## Remove copied mount identity tuples (no second owner)

Mount crossing (`lookup_step`), mounted `readdir`, direct bootstrap
visibility checks and unmount validation now all derive their mounted
parent's identity from **the same counted Path Dentry**. The redundant
`parent_mount`, `parent_mount_id`, `parent_ino`, backend-pointer
copy, parent/covered inode snapshot and anchored boolean were deleted
from `mount_slot`. The mount's Path owner is the object of record,
not a copied identity checked separately by each algorithm.

The actual parent node is read from its Dentry only while the mount's
Path reference is held. The covered inode is likewise read from the
owned covered Dentry. Names remain independently stored only for
virtual mountpoint directory entries (an intentional namespace label,
not a duplicate inode owner). A preflight guard forbids reintroducing
the retired tuple fields. A future Mount namespace can reuse this
single identity model without structural changes to providers.
