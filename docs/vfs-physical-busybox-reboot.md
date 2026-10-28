# Real third-party filesystem + physical disk: two native boots per architecture

## Verified architecture boundary

The physical data path is **actual original U-Boot VirtIO-Blk** through
NaSeed BlockDevice `read/write/flush`, then original lwext4 and the
same common Linux syscall VFS as tmpfs, RamFS and LittleFS:

`original Debian BusyBox -> syscall -> VFS -> lwext4 -> BlockDevice ->
original U-Boot VirtIO-Blk -> VirtIO PCI/MMIO -> raw ext4 QEMU drive`.

`ports/lwext4/guest_boot.c` currently probes registered `virtio0..N`
devices and mounts a whole-device ext4 superblock (LBA 2 for 512-byte
sectors) at `/mnt`; the EFI FAT boot drive is NOT mistaken for ext4.
This is a real early boot mount, **not** a general `mount(2)` syscall,
a partition driver or a device namespace. These three capabilities
are still separate outstanding architecture work and are NOT claimed
complete by this acceptance.

## Real userspace workload (unchanged third-party ELF, two cold boots)

The pinned original Debian BusyBox 1.37.0 for each ISA runs the **same**
`tests/vfs-busybox-disk-workload` via BusyBox `sh`, using
unmodified `cp`, `cmp`, `mv`, `tar`, `find`, `grep`, `rm`,
`rmdir`, `cat` and `sync` applets. Each architecture owns one
freshly cloned host-generated ext4 volume:

- Boot #1 (`disk-create`): nested mkdir, file write+read,
  recursive copy, rename, archive creation, traversal and
  marker `created-before-reboot`.
- The kernel closes its VFS File owners, lwext4 unmounts, and the real
  block driver flushes. QEMU exits. **The host does not rewrite/repair
  the volume or supply a Limine in-memory ext4 copy.**
- Boot #2 (`disk-replay`) reuses the exact SAME raw virtual disk,
  verifies pre-reboot contents, extracts the persisted archive,
  compares files, removes directories and writes
  `verified-after-reboot`.
- Only after the second clean boot does the Linux host run
  `e2fsck -fn` (including checking for silent count/bitmap mismatch),
  and read persistent metadata/data via actual `debugfs`.

The original unmodified BusyBox binary and exact same program are
also run under the Linux host oracle (amd64) before the guest.

The test reuses the existing proved whole-kernel binaries and
architecture-independent BusyBox native parity harnesses:
`scripts/probe-vfs-busybox-physical.sh` (amd64/riscv64/arm64).
The physical-drive `NAS_VIRTIO_BLK_IMAGE` goes to QEMU
virtio-blk-pci or virtio-blk-device (NOT to `module_path`).
The ESP remains a separate FAT boot device in the ARM/RV case.
No ISA-specific filesystem layer or new handwritten disk driver.

## What real application exposed in the public syscall contract

Original BusyBox `sync` is a Linux syscall and does NOT accept a file
descriptor. Previously NaSeed had only `fsync(fd)`, but no shared
`sync()` operation, which is a real application-visible POSIX
compatibility gap. The Linux ABI decoder now maps `__NR_sync=162`
on x86 and Linux asm-generic `81` on ARM64/RV64 into a **common**
`NAS_SC_sync` semantic operation. A single VFS
`nas_vfs_sync_all()` walks its OWNED active mounts, obtains each
provider's root inode, and flushes every writable mount through its
existing durability callback. It never treats RAM-mounted FS as
persistent, never fakes a writable ext4 flush when a device lacks a
flush callback, and does not enumerate VirtIO devices in syscalls.
Failures are reported rather than invented successes.

## Remaining architectural gaps

1. The physical ext4 early boot probe mounts a **whole device**.
   Partition offset detection (GPT/MBR) and safe sub-block-device
   ownership are still to be implemented below filesystem drivers.
2. Public mount/umount, device nodes and permissions require explicit
   Linux VFS namespace/privilege contracts. Early boot mount is not
   a synonym for Linux `mount(2)`.
3. The current mount registry has 16 slots and the pathname limit
   is 256 bytes; dentry-owned mount tree and directory-anchor models
   need the separate VFS architecture work documented in
   `docs/vfs-architecture-audit-2026-10-28.md`.
4. `sync()` across all active mounts is synchronous and single-CPU
   in the current VFS; SMP, error reporting and hot-removal ordering
   must be revisited with concurrency.
5. Real SQLite and Git against the same physical ext4 and recovery
   after a *forced power loss* are separate advanced tests. A clean
   reboot only establishes normal durability, not crash safety.

**Acceptance policy:** A CI run PASS requires BOTH real guest phases,
positive application stdout + status and clean unmount, followed by
host fsck and debugfs of the same disk bytes. A kernel selftest or a
RAM-backed ext4 pass alone is insufficient.
