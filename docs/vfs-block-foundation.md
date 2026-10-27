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
