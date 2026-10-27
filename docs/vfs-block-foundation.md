# VFS / Block / lwext4 integration boundary

Baseline: NaSeed `main@9bed1c3a` after the Task/MM architecture consolidation.
Historical reference: NaOS `98fa26a2` ("移植lwext4"), later Linux-style VFS.
This change adds actual linked BlockDevice and RamDisk implementations. It
**does not** claim ext4 is mounted, a VirtIO driver exists or that VFS has
already been migrated. Those require separate evidence.

## Architectural invariants
- BlockDevice owns geometry, write policy and driver I/O callbacks, **not**
  inode, dentry, mount, Linux syscall or userspace memory policy.
- Filesystem mounts pin a block device using get/put. Detaching a pinned device
  fails with EBUSY. RAM-backed images are borrowed, not copied or freed.
- Sector offsets/counts are bounds checked before dispatch; long requests
  split at each driver's advertised limit; I/O errors propagate. Flush does
  not return false success when a writable driver lacks a flush callback.
- Every CPU architecture links the **same** `kernel/block/*.c` objects.
  VirtIO PCI and MMIO will be transport-specific providers below this ABI.
- Storage and FS-specific code MUST NOT be added to the common syscall decoder.

## Planned path to the first real ext4 mount
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
Upstream lwext4 includes mixed BSD-3-Clause and GPLv2 sources. Before
vendoring, pin an exact revision and audit license compatibility against
the NaSeed repo's declared project license. Do not blindly copy NaOS files.

## Registered VFS provider milestone
- Common pathwalk now asks longest-prefix registered filesystem mounts for
  inode views. Its symlink walk, ELF kernel-buffer reader and path metadata
  calls use provider operations, not a bootfs/tmpfs/procfs kind enum.
- bootfs, tmpfs and procfs are registered together; /tmp and /proc retain a
  documented temporary read-through to immutable CPIO for old initramfs
  compatibility. New persistent filesystems must use strict mount semantics.
- This is **not** yet a fully refcounted Linux dentry/superblock/mount tree;
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
- License caveat: upstream contains GPL-2.0-only `ext4_xattr.c` and
  `ext4_extent.c` as well as BSD-3-Clause files. Its exact original
  `LICENSE` and file-level headers must be retained. Check the
  distribution-license compatibility of a linked NaSeed kernel BEFORE
  landing any guest-side lwext4 integration, and do not copy NaOS GPL-3
  source into the same combined work without resolving compatibility.

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
