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
