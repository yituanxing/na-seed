# VFS / Block / lwext4 integration boundary

Baseline: NaSeed `main@9bed1c3a` after the Task/MM architecture consolidation.
Historical reference: NaOS `98fa26a2` ("移植lwext4"), later Linux-style VFS.
The pinned upstream lwext4 is mounted via NaSeed's common VFS/BlockDevice
and RamDisk across x86-64/RV64/AArch64.

## Current source boundary (2026-10-30, `221c46a`)

- The current branch **does have a real physical-QEMU VirtIO-Blk path**:
  `kernel/drivers/virtio/{mmio,pci}.c` share the transport/queue layer,
  `ports/uboot/blk_port.c` uses the pinned original U-Boot request engine,
  and `ports/lwext4/` mounts through the same BlockDevice ABI. Three-ISA
  guest disk persistence, clean unmount/flush and host strict e2fsck have
  dedicated CI acceptance. Do not call this physical-host or IRQ-driven
  production hardware support: current I/O remains bounded synchronous
  polling and QEMU emulation.
- Normal non-root pathname lookup is **component-based** through the one
  `kernel/fs/lookup.c` namei path, with owned `nas_vfs_path`/positive Dentry
  references and counted mount attachments. `mount_matches()` remains only
  in the scoped historical CPIO `/tmp` overlay fallback, not as a general
  longest-prefix dispatch algorithm. This is not a Linux-equivalent SMP,
  negative-dentry or mount-namespace implementation.
- The older plan and milestone paragraphs below are a **chronological
  implementation record**, not the present TODO list. In particular their
  statements about no physical VirtIO-Blk, no owned Dentry, and pending
  first guest ext4 mount have been superseded. For current acceptance and
  remaining limits see `docs/vfs-real-program-acceptance.md`,
  `docs/vfs-lifecycle-linux-naos-review.md`, and the exact-head Actions.

At the early handwritten-driver removal checkpoint, commit `0c93f3ac`
had passed AArch64 physical VirtIO-MMIO ext4 persistence. That temporary
engine was deleted before the pinned U-Boot source-preserving port replaced
it; the intervening no-physical-driver state was temporary.

## Architectural invariants
- BlockDevice owns geometry, write policy and driver I/O callbacks, **not**
  inode, dentry, mount, Linux syscall or userspace memory policy.
- Filesystem mounts pin a block device using get/put. Detaching a pinned device
  fails with EBUSY. RAM-backed images are borrowed, not copied or freed.
- Sector offsets/counts are bounds checked before dispatch; long requests
  split at each driver's advertised limit; I/O errors propagate. Flush does
  not return false success when a writable driver lacks a flush callback.
- Every CPU architecture links the **same** `kernel/block/*.c` objects.
  VirtIO PCI and MMIO are transport-specific providers below this ABI.
- Storage and FS-specific code MUST NOT be added to the common syscall decoder.

## Historical plan to the first real ext4 mount (now implemented)
1. Migrate existing bootfs, tmpfs and procfs to real VFS
   `super_block / inode / dentry / mount / file` object lifetimes, preserving
   existing user programs. Replace filesystem-kind conditionals with provider
   operation tables; remove obsolete code as ownership transfers.
2. Pin a memory-resident ext4 image using `nas_ramdisk_attach`. Introduce the
   pinned lwext4 upstream and a narrow `ext4_blockdev_iface` adapter to
   `nas_block_{read,write,flush}`. lwext4 owns ext format; VFS owns mounts.
3. Verify host-generated ext4 disk images via REAL read/modify/unmount/remount
   and `e2fsck`, including unlink-open, hardlinks and rename. No invented PASS.
4. Add shared VirtIO virtqueue/device core, then ISA-specific PCI/MMIO
   transports and a VirtIO-Blk provider. Re-run the same ext4 tests with
   persistent QEMU disk images on x86-64 / RV64 / AArch64.
5. Validate kernel-buffer VFS reads for execve and mmap; keep usercopy solely
   in syscall ABI adapters. Preserve grouped native acceptance milestones.

## Licensing
The already-pinned lwext4 source has BSD-3-Clause notices and two files,
`ext4_xattr.c` and `ext4_extent.c`, whose own headers explicitly allow
**GPL version 2 or any later version**. The bundled lwext4 `LICENSE` contains
GPLv2 and defers to individual file notices where they differ. NaSeed has
no repository-level `LICENSE` as of this checkpoint; a first-party licensing
decision and a linked-kernel distribution review remain open. Do not blindly
copy NaOS files. See `docs/distribution-license-review.md`.

## Historical registered VFS provider milestone (superseded)
- At this earlier stage, common pathwalk asked longest-prefix registered filesystem mounts for
  inode views. Its symlink walk, ELF kernel-buffer reader and path metadata
  calls use provider operations, not a bootfs/tmpfs/procfs kind enum.
- bootfs, tmpfs and procfs are registered together; /tmp and /proc retain a
  documented temporary read-through to immutable CPIO for old initramfs
  compatibility. New persistent filesystems must use strict mount semantics.
- At that stage this was **not** yet a counted VFS Dentry/Path owner graph;
  no claim of safe unmount/open-FD lifetime, ext4 disk execution or complete
  FD migration. Those require a later batch and real workload acceptance.

## Shared open File/mount ownership extension
- Mounted provider inode views are attached to the existing reference-counted
  nas_open_file, rather than each task-local FD. dup/fork/SCM_RIGHTS aliases
  share identity and one mount pin. Final File release returns exactly one pin.
- CPIO and tmpfs positional read, streaming read and MAP_PRIVATE now reuse
  VFS kernel-buffer read_at. Unlink/rename cannot retarget opened inodes.
  Raw Limine module descriptors remain a documented temporary compatibility
  exception.
- Unregister refuses pinned or parent mounts; bootstrap mounts permanent.
  Full superblock/dentry/inode caches and concurrent umount remain TODO.

## Vendored upstream lwext4 (source owned by third-party authors)
- Pinned exact upstream commit: `58bcf89a121b72d4fb66334f1693d3b30e4cb9c5`.
- Kept intact under `third_party/lwext4/{include,src,blockdev}`, **never
  flattened into kernel/fs or edited as if our own native VFS code**.
- NaSeed's translation boundary is `ports/lwext4/lwext4_block.[ch]`;
  block drivers do not call ext4 APIs.
- Host test compiles this vendored upstream **offline**; no shallow runtime
  Git clone of a moving upstream branch.
- License caveat (historical import gate): upstream contains GPL-2.0-**or-later**
  `ext4_xattr.c` and `ext4_extent.c` as well as BSD-3-Clause files.
  Its original `LICENSE` and file-level headers must be retained. The guest
  integration has since landed and passed QEMU; the remaining gate is a
  documented combined-work license and notice review **before a distributable
  kernel release**, not retroactive proof that guest integration was illegal.
  NaOS GPLv3 source remains an architectural reference, not vendored code.

### Freestanding build boundary
The upstream-to-NaSeed ABI adapter resides in `ports/lwext4/`. This is a
permanent **port** directory, not extra code that the kernel-wide compilation
glob should opportunistically pull in before a guest kernel allocator and the
lwext4 filesystem implementation are linked. Host integration builds this
port directly. Guest kernel integration must explicitly add the port and the
required lwext4 source targets at the same time, on all architectures.

The vendored original files retain their exact bytes, including upstream
whitespace. Git checks are disabled *only for this isolated vendor subtree*
by `.gitattributes`; all first-party source retains strict whitespace
checks.

### Freestanding lwext4 runtime bridge (this batch)
The upstream library is compiled with a separate, explicit configuration
in `ports/lwext4/freestanding/platform.h` (OWN_ERRNO, user malloc, no
host libc printf). `compat.c` forwards allocation to the new general-purpose
kernel heap, and provides the only missing C helpers. The heap supports
non-overlapping additional arenas without changing the allocator API.

All 21 real upstream C units and the port compile on x86-64, RV64 and ARM64
with the permanent freestanding headers; the host test exercises actual
allocator allocation, double-free, realloc, merging and arena extension.
This compilation gate is **not** yet guest lwext4 mount/link evidence.

### Full cross-target guest linkage gate
`scripts/check-lwext4-guest-link.sh` compiles and **links** every vendored
lwext4 source file, NaSeed BlockDevice bridge, heap and freestanding C
compatibility source as one *relocatable* object for each of the three actual
ISA ABIs. It rejects unresolved libc and compiler runtime symbols. This is
actual guest-target link compatibility, **not yet** attaching and mounting
ext4 in a booted NaSeed kernel. The single relocatable object can be passed
to the native kernel link when its VFS provider and mount lifecycle are ready.

### Real kernel ELF linkage, opt-in
Set `NAS_ENABLE_LWEXT4=1` to link full upstream lwext4 into the actual
NaSeed kernel ELF for x86-64, RV64 or AArch64. The central build script is
`scripts/build-lwext4-guest.sh`; its `kernel` mode EXCLUDES the common
`string.c` and `block/device.c` implementations, preventing duplicate
symbols/registries. Default CI remains unchanged until the actual guest
VFS mount and syscall workflows are proven. A successful native kernel ELF
link **does not** establish a successful booted ext4 mount.

### Real ext4 VFS provider (same code on host and kernel)
`ports/lwext4/guest_vfs.[ch]` now binds a real ext4 image to the registered
VFS mount model, supplies inode-number-based positional reads and Linux inode
mode/size, and supports readlink via the current dentry pathname. Mount
operations get explicit instance state from `mount->private_data`, not a
global singleton or an if/else filesystem type branch.
The first registration is **intentionally read-only** until the generic VFS
write/create/unlink File operations and lifetime are fully migrated.
The host ext4 acceptance now exercises `nas_fs_lookup_node` and
`nas_vfs_read_at` over real ext4 blocks and validates busy unmount. The
optional kernel link includes this provider, but a booted guest ext4 mount
and Linux syscall workload still require a separate exact-head run.

### First actual guest boot acceptance (x86-64)
With `NAS_ENABLE_LWEXT4=1`, the real `nas_init_userspace()` stage
discovers a separately supplied Limine module with an on-disk ext4
superblock, registers a RamDisk and the same VFS mount at `/mnt`,
resolves a real directory and optionally reads the pinned real-ext4 fixture.
The dedicated QEMU script builds an actual UEFI ISO with a guest userspace
ELF plus ext4 image and requires both a real guest VFS read marker and the
independent original M1 userspace exit. Not a host-native simulation.

Other ISAs compile/link the same port; QEMU boot execution on RV64/AArch64
and Linux syscall openat/read/getdents on mounted ext4 remain separate gates.

### Real ring-3 ext4 syscall acceptance
`user/ext4-reader.S` is a real separate ELF loaded by Limine PID1 in
the opt-in x86 kernel. It opens `/mnt/sandbox/hardlink` via Linux
`openat`, checks `fstat` length, reads bytes via Linux `read`, resets
position with `lseek`, verifies the same content after re-read, sends
its real bytes to the UART via `write`, closes, then exits 0. The CI
script demands both the expected file content and native PID1 completion
marker. It does NOT count a successful kernel-only ext4 read as syscall
acceptance.

### Native RV64/AArch64 guest acceptance
The same optional Limine ext4 module, VFS and upstream lwext4 guest bootstrap
now run in the existing **real** UEFI/OpenSBI RV64 and EL1/EL0 AArch64 QEMU
launchers. No cloned filesystem probe or separate target-specific algorithm:
`NAS_LWEXT4_TEST_IMAGE=...` appends the authentic host-generated disk image,
while `NAS_ENABLE_LWEXT4=1` links the common port. Both runners demand the
in-guest VFS inode-read marker plus their original Linux userspace ELF
exit markers. Normal non-ext4 runs remain unchanged. Does not constitute
`getdents64` or writable ext4 syscall acceptance yet.

### Generic directory operation and real ext4 enumeration
The kernel now defines `nas_vfs_dirent` plus a `read_dir` provider operation
independent of Linux dirent64 bytes. The real upstream ext4 iterator yields
inode numbers and file types for children, with logical cursor units shared
by open descriptions. It is a batched immutable-directory implementation,
not yet an inode-based rename-safe iterator for future writable mounts.

Linux `getdents64` now formats provider-supplied directory entries in
kernel-owned bounded batches with shared File cursor ownership and Linux ABI
validation at the syscall boundary. x86 ring3 acceptance verifies the real
hardlink name returned through the ext4 mount. The existing bootfs/tmpfs and
procfs listing adapters remain pending migration to the same read_dir callback.

### Mounted directory navigation and link resolution
The common path namespace resolves `chdir` via VFS namei, and supports
dirfd-relative `openat` and `fchdir` on an opened mount-backed directory
whose open description retains its VFS inode and canonical path. Linux
`readlinkat` likewise asks the inode's VFS readlink operation, preserving
procfs /proc/self/fd dynamic link handling. `lseek` accepts VFS directory
descriptions (SEEK_SET/SEEK_CUR) rather than assuming CPIO ownership.
The real ring3 ext4 reader exercises both dirfd-relative and fchdir-relative
openat, alongside getdents64.

### Authentic application-based ext4 acceptance
The specialized x86 UEFI QEMU gate additionally executes the **original**
Debian 13 BusyBox 1.37.0-6+b9 static ELF from the repository's existing
SHA256-protected multi-architecture fixture, against the mounted ext4 image:
`busybox cat /mnt/sandbox/hardlink` and `busybox ls -l /mnt/sandbox`.
This exercises actual library/system-call expectations and directory
enumeration via Linux `getdents64` and file metadata; the image and BusyBox
were not built inside NaSeed. Legacy 3-ISA tests remain unchanged.

## One VFS directory data path for all four providers

The read_dir operation is now supplied by bootfs, tmpfs (including the legacy /tmp overlay root), procfs and lwext4. The syscall module has exactly one Linux getdents64 translation; providers only produce bounded kernel-owned dirents and no longer write Linux user memory. Existing `/tmp` archive fallback is explicitly kept for namei, with tmpfs-only directory listing as before. Shared open-description cursors keep fork/dup offsets consistent. The host fixture exercises all providers, child types and pagination. This batch does not claim writable ext4 or a fully refcounted dentry tree.

## Upstream-first driver boundary: old backend removed

- Removed custom `kernel/drivers/block/virtio_mmio_blk.[ch]`, its unit-test
  and provisional AArch64 driver-probing platform glue. No shadow fallback.
- Kept shared `kernel/block/device`, `kernel/drivers/port/block`,
  `kernel/drivers/core/bus`, `kernel/drivers/port/dma`, and
  `kernel/drivers/virtio/{transport,mmio}`. These are existing interfaces,
  not evidence that a working VirtIO-Blk driver has been imported.
- Removed reverse dependency where `ports/lwext4/guest_boot.c` probed a
  hardware transport. The boot filesystem only mounts its Limine RamDisk.
  Physical discovery/driver binding must be established *before* mounting.
- Pinned original Unikraft driver is present unchanged at
  `third_party/unikraft/drivers/virtio_blk.c` under a permissive license.
  Needed integration dependencies include upstream virtqueues,
  `uk_blkdev`, `uk_sglist`, memory management and request completion.
  Define adapter boundaries for those, without rewriting another queue
  engine from scratch under a different filename.
- NaOS provides useful architectural reference but its repository is GPLv3;
  do not copy its source into the Unikraft port without license review.

**Acceptance is deliberately fail-closed:** `NAS_ENABLE_VIRTIO_BLK=1`
reports `VIRTIO UPSTREAM PORT REQUIRED`, and the existing AArch64
persistent QEMU/e2fsck CI step is NOT removed or weakened. The host
BlockDevice/transport, three-ISA RamDisk ext4, and userspace QEMU gates
must continue to pass while the mature driver is properly integrated.
A future driver must prove real request dispatch, flush/power-off image
persistence, and exact-head native acceptance before merging the PR.

## NuttX Apache-2.0 VirtIO-Blk C request-port (2026-10-27)

Pinned reference: Apache NuttX `drivers/virtio/virtio-blk.c` at
`0884a521e587198fa303611c5f13a154b6f6d196`.
`ports/nuttx/virtio_blk.c` is explicitly a NaSeed **adaptation**, NOT the
original unmodified NuttX driver. It carries Apache-2.0 attribution.
The NuttX request flow (header/data/status, sectors, RO, flush) runs on
NaSeed's one BlockDevice, shared VirtIO transport, upstream Unikraft wire
headers/split ring and coherent QEMU DMA provider. No new VFS/ISA syscall,
parallel device registry or bespoke MMIO request engine was added.
The kernel buffer is bounced through one DMA page with an independent
header/status page; 512/1024/4096 logical block sizes share the same code.

The host mock dispatches and completes the **actual** port's descriptor
requests against a byte array, verifying real BlockDevice read/write/flush,
read-only refusal, I/O error propagation, attach/detach mount pins,
unsupported writable flush rejection and bounded timeout/reset.
All three ISA C targets share this port and compile it in syntax-only mode.
**This does not establish a booted kernel, physical VirtIO-Blk or ext4 on a
real QEMU drive.** Discovery/transport mapping, guest link, physical disk
read/write persistence and e2fsck must still pass before enabling by default.

## Opt-in AArch64 physical guest integration

Restored the previously proven QEMU virt modern MMIO device *discovery* and
actual ext4-superblock volume selection, but the removed handwritten block
request engine stays deleted. The bus now binds `ports/nuttx/virtio_bus.c`
which calls the same NuttX-derived generic C request port used by the
host tests. The same `kernel/block` and `ports/lwext4` mount path serves
RAM and physical disks. No transport address enters filesystem code.
Only `NAS_ENABLE_VIRTIO_BLK=1` opts into the hardware provider; default
native x86/RV/ARM regressions remain unchanged.

Acceptance requires the existing **real AArch64 QEMU** VirtIO-MMIO guest
stage to run a genuine userspace ext4 write and flush and for host `e2fsck`
and `debugfs` to verify the persisted bytes afterwards. Source link alone
is not acceptance. No x86 PCI or RV64 MMIO hardware provider claim yet.

## QEMU virt MMIO bus layout — physical RV64 regression correction

Hardware gate 40593565455 verified original 3-ISA RAM-ext4 and genuine AArch64
physical disk persistence, then reported RV64 `scause=5` at the *second*
virtual MMIO slot (virt+0x200): an unmapped QEMU device-register region,
not a generic VFS or block protocol error. ARM QEMU virt exposes 32 modern
MMIO slots at **0x200 stride**; RISC-V virt exposes **8 slots at 0x1000
stride**. The common scanner now takes platform-supplied stride/count and
maps the complete device window. No architecture check is added to the
shared VirtIO request engine, VFS, or block layer. The RV64 QEMU test
remains red until its exact-head re-run passes real persistence/e2fsck.

## PID1 clean-unmount and strict on-disk fsck (2026-10-28)

Earlier hardware runs proved persisted file bytes, but raw `e2fsck -fn`
also emitted free-block and free-inode count mismatches; the step had
previously only checked the tool's exit code. Those passes **did not prove
clean ext4 metadata**.

The native boot-test exit now invokes the existing `nas_lwext4_unmount`
after PID1's last Files are closed. This calls upstream `ext4_umount` and,
critically, a **second hardware BlockDevice flush** after ext4's final
superblock/bitmap writes, before releasing the drive. The same code
handles RAM or physical disks and all CPUs. Physical-disk QEMU logs must
print the `clean upstream unmount and durable flush` marker.

`scripts/check-ext4-fsck-clean.sh` now rejects both nonzero `e2fsck -fn`
and textual metadata/accounting inconsistencies, before verifying the
original persisted payload using `debugfs`. **Do not mark this complete
until the exact-head real QEMU job is green.**

## 2026-10-28: first source-preserving driver switch (NaOS-inspired design)

After auditing NaOS's entire VirtIO bus, MMIO, PCI, split queue, driver
module and BlockDevice paths, the working AArch64/RV64 production opt-in
uses **exact original U-Boot GPL-2.0+ VirtIO-Blk C source**, not our old
NuttX-inspired rewritten read/write driver. U-Boot's original source is
compiled into ports/uboot/blk_port.c. The NaSeed compatibility implementation
handles the stable device model, transport, bounce DMA and VirtIO queue
hook, plus a small fsync-required flush adapter. NaOS source itself cannot
be assumed directly reusable merely because the lwext4 files offer a
GPLv2-or-later option: provenance, per-file grants and the intended
first-party license still need checking. The old assumption that those
two lwext4 files were GPLv2-only was incorrect. See
`docs/virtio-upstream-provenance.md`.

This is a **controlled driver replacement** behind the same hardware
discovery, block ABI, ext4, VFS and userspace: no filesystem/architecture
special cases are allowed. Retain the legacy alternative only for host
comparison until physical QEMU + strict e2fsck success proves the new code.

## Clean-up of superseded handwritten block request logic (2026-10-28)

After real AArch64 and RV64 QEMU verified the unchanged U-Boot GPL-2.0+
request handler and clean ext4 persistence, the previously linked custom
NuttX-inspired VirtIO-Blk source and its isolated host test were removed.
The unlinked Unikraft blk .c port candidate was also removed; the wire
header and vring layout actually used by the queue remain pinned unchanged.

Both actual QEMU block boots now compile exactly one original upstream
VirtIO-Blk read/write engine. Common NaSeed BlockDevice, VFS, lwext4,
VirtIO transport, DMA and split-ring adapter are unchanged, so the test
controls the *driver implementation* variable against an independently
maintained codebase, as required by the NaOS-style architecture review.

## 2026-10-28 VFS lifetime fail-closed audit (first coherent batch)

The actual upstream `ext4_fremove` immediately truncates data and frees the
inode when the last directory link is removed. A pre-existing NaSeed open
File still keeps only a `(mount, inode)` lease, so permitting this operation
would leave dangling open descriptions. The VFS now checks the actual live
last-link inode lease and **rejects unlink-open with EBUSY** for providers that
have not implemented safe inode orphan/deferred reclamation. The existing
RAM tmpfs explicitly declares its current unlink-open lifetime support;
other providers default to fail-closed. The pinned ext4 integration test
exercises an open reader through rejected unlink and continued reads.
**This is NOT POSIX-complete**: full open-unlink semantics still require
owned inode/dentry objects and deferred reclaim, not an EBUSY workaround.

On a failed *post*-ext4_umount hardware cache flush, the adapter now finishes
detaching its BlockDevice and any owned RamDisk instead of leaking a pinned
controller (the unmount has already irreversibly finished upstream). It
still reports the flush error. Earlier pre-umount errors preserve a mounted
namespace and allow retry. This must be checked independently of the
regular successful shutdown gate.

Upstream FS candidate review: Apache NuttX tmpfs is highly coupled to the
NuttX VFS and Apache-2.0-licensed (a concern if the eventual linked work
were restricted to GPLv2-only, which the pinned lwext4 file headers do not
establish). Linux tmpfs is GPL-2.0 but coupled to Linux page-cache/shmem. Instead
we will **compare with BSD-3-Clause littlefs** on a borrowed memory-backed
BlockDevice as a *second independent external file system*, not a replacement
for RAM tmpfs and not an ext4 clone. Its path-based lookup has no stable
POSIX inode API, so it may NOT claim unlink-open/rename-open until per-open
VFS object ownership is implemented. Keep original upstream sources pinned
and add a standalone real-provider host gate before guest activation.

## 2026-10-28 — inode-stable directory enumeration and replacement policy

Mounted lwext4 getdents now reads the actual upstream directory by the
pinned ext4 inode number, using the **original ext4_dir_entry_next()**;
it no longer opens each batch by the obsolete FD origin pathname. A live
open directory survives a same-mount rename, and rmdir-open refuses EBUSY
until the directory ref is released. The VFS also guards rename-over a
**different pinned victim** by default, as upstream ext4 replacement can
otherwise free data still referenced by the victim File. A provider must
explicitly advertise safe rename replacement to opt out, otherwise it
fails closed with EBUSY. This extends the previous unlink-open guard;
it does **not** implement full Linux POSIX orphan inode reclamation.

The host real-ext4 test adds directory pin/rename/getdents/cleanup and
independent pinned-victim rename rejection + surviving inode reads. This
preserves current File/FD ABI and does not fork VFS algorithms per ISA.

## 2026-10-28 — tmpfs directory dentry follows live inode

The native tmpfs provider now resolves a renamed directory's *current*
namespace dentry from its stable inode via `tmp_name()`, not the obsolete
per-File `origin_path` used when opening the FD. The separate generic
VFS/provider host fixture covers pinned directory + rename + stale-name
readdir, and ext4 exercises the same test with upstream inode-based dir
iteration. Production native tmpfs namespace owns the dentry tree and
existing path mutations; this change adds no duplicate path storage.

## 2026-10-28 — inode-validated live directory paths (VFS-wide)

Generic `nas_vfs_directory_path(inode, origin_hint, out)` resolves the current
name of a pinned directory. Providers MAY supply `directory_path` for a
native inode->dentry mapping; native tmpfs uses its actual live dentry and
bootfs uses immutable archive identity. The VFS ALWAYS verifies the
resolved path against the original `(mount, ino)` via real namei before
returning it to an ABI layer. This prevents a stale FD origin pathname
from silently being repointed to a *different* directory after a rename.
The shared Linux `openat`/namespace dirfd resolver, `fchdir`, cwd-relative
resolution and `getcwd` use this one VFS API rather than backend-specific
`tmp_name`/boot-entry/origin-path branches. For filesystems with no way to
recover a moved dentry (currently lwext4), dirfd-relative operations on
an old name return **ESTALE** instead of accessing a recycled directory;
getdents64 remains inode-relative and survives rename. This is explicit
partial semantics, not complete Linux dcache/rename compliance.

Host tests cover renamed tmpfs directory live-path recovery, immutable
bootfs directory identity, ext4 moved directory fail-closed and new-name
validation, and real LittleFS read-only directory path identity. No new
ISA-specific FS algorithm or modification of pinned upstream sources.

## 2026-10-28 — native ext4 directory live path by inode parent chain

Unlike the first conservative `ESTALE` fallback, lwext4 now implements the
same provider-neutral `directory_path` callback using original upstream
`ext4_dir_entry_next` over the pinned inode and its on-disk `..` parent
entry. Each ancestor parent is scanned to recover the child's live name,
then joined under the existing VFS mountpoint. This does not rely on a
File's stale pathname or modify upstream code. Depth and output are
strictly bounded, cycles and disconnected directories fail closed.

One REAL ext4 writable host regression pins an open directory, renames it,
recreates a different inode at the opening pathname and verifies the old
FD still maps to the new name (and NOT the recycled directory). Common
`nas_vfs_directory_path` independently validates the final mount+inode.
This enables shared `openat`, `fchdir`, cwd and getcwd to follow ext4
renames just like native tmpfs; no ISA/syscall knowledge in lwext4.

## 2026-10-28 — genuine one-source three-ISA Linux dirfd inode lifetime regression

The existing **same** `user/ext4-guest-rw.c` unprivileged Linux ELF now
exercises the full VFS directory lifetime across x86-64, RV64 and AArch64,
including the real physical VirtIO ext4 disk (not just an isolated host
fixture). It mkdirat()s a new directory, opens an O_DIRECTORY FD, renames
the directory, *recreates the old path as a different inode*, performs
openat(dirfd, relative O_CREAT) and fsync, reopens at its moved absolute
path, then fchdir(dirfd), closes the directory FD and validates getcwd()
is the **new** name via an independently pinned cwd inode. The syscall ABI
operation-number tables only extend existing Linux wrappers; all previous
operation indices remain unchanged. Native ARM64 O_DIRECTORY uses the
previously tested target-UAPI flag, not x86 flag magic. Same content and
checks execute on all three native QEMU kernels for both volatile RamDisk
and actual VirtIO ext4 as part of the existing full integration suite.

## 2026-10-28 — tmpfs orphan inode finalization uses VFS ownership

Audit found that native tmpfs `tmp_try_reclaim()` only checked legacy
`tmp_slot_owner` open File slots; a directory could be `rmdir()`ed while
held solely by the process `cwd_node` or a generic VFS File and be
prematurely freed. This is a **real use-after-free** risk. The generic
VFS inode-size lease now retains an opaque backend-inode identity and
exposes a pure `nas_vfs_inode_is_pinned()` query to providers. tmpfs
reclamation refuses to free a last-link orphan while *any* such VFS lease
exists (in addition to existing FD-flock/slot checks). A new optional
`last_inode_unpin` provider hook runs exactly after the last shared
mount+inode lease drops, allowing native tmpfs to reclaim an orphan even
if its final owner was the cwd FS context rather than a File. The synthetic
`/tmp` root is explicitly excluded because its inode is owned by bootfs.
No changes to syscall architecture, fixed ISA selection, lwext4, littlefs
or pinned third-party implementations. The generic VFS host oracle tests
2 independent owners, deferred orphan release, and one final callback.
