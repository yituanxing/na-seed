# Provenance and integration decisions: NaOS vs U-Boot original

**NaOS read in full before altering this branch:** `aether-os-studio/na-kernel`,
current source directories:
`kernel/src/drivers/virtio/{virtio,queue,mmio,pci}.[ch]`,
`modules/drivers/block/virtio-blk/blk.[ch]`,
`kernel/src/block/block.[ch]`. The NaOS public module uses ONE
`virtio_driver_op_t` with MMIO and PCI implementations, a single
`virtqueue_t` path, IRQ-completion + wait queues, allocated request slots,
DMA bounce buffers, and ONE exported block-device read/write/flush callback
set. NaSeed must preserve those **architecture boundaries**, never one
independent block driver for each ISA.

NaOS current kernel repository top-level LICENSE is **GPLv3**. Its block and
queue sources carry copyright notices but no separate per-file GPLv2 grant
in the inspected source. Pinned GPLv2-only lwext4 sources cannot be assumed
compatible with copying GPLv3-only code into their single linked kernel.
Therefore, **do not copy NaOS C driver code into a linked NaSeed kernel
without obtaining suitable permission or verifying an applicable license**.

## Actual upstream source reused unchanged

U-Boot `u-boot/u-boot` Git revision
`ece349ade2973e220f524ce59e59711cc919263f`,
`drivers/virtio/virtio_blk.c` SPDX **GPL-2.0+** and the original
`virtio_blk.h` SPDX **BSD-3-Clause**. They are retained verbatim at
`third_party/uboot/drivers/virtio/`. A CI hash check enforces identical
upstream bytes; **this original .c is compiled in the production opt-in
guest**, not merely copied into a folder.

`ports/uboot/compat/*` provides only U-Boot's minimal device-model types,
heap allocations and virtqueue hooks over NaSeed's existing generic bus,
Transport, DMA, and upstream Unikraft vring. `ports/uboot/blk_port.c`
uses the real upstream `virtio_blk_probe/read/write`; it does not reimplement
their read/write/sector/SG protocol. The sole extension is a VirtIO FLUSH
command through the same upstream U-Boot virtqueue API: U-Boot's bootloader
driver does not itself expose fsync. Transport/CPU-specific paths are not
modified.

Restrictions deliberately fail closed: one outstanding 4096-byte bounced
request; modern split ring only; coherent DMA only; bounded polling with
transport reset on errors; RO and flush requirements; no stale DMA release.
This is an explicit correctness subset rather than an unbounded approximate
port of all U-Boot features. For multi-segment hardware size constraints,
attach returns unsupported until an upstream-compliant extension exists.

The superseded NuttX-*inspired* hand-written block engine and its tests
have now been **deleted** after original upstream source passed the real
AArch64/RV64 physical QEMU ext4 write/fsync/clean-e2fsck acceptance. No
parallel bespoke VirtIO-Blk request backend remains in the repository.

**Acceptance:** exact-head host + 3 ISA compile, real AArch64 and RV64 QEMU
physical disks, true userspace ext4 write/fsync/reopen, final clean unmount,
strict host fsck, and persisted bytes. If any fails, do not claim completion.

## Post-integration native evidence (2026-10-28)

Exact-head `a5b8ca627d8cefde4b87ffb5e579b67888994c44`:
[NaSeed lwext4 workflow #40602875005](https://github.com/yituanxing/na-seed/actions/runs/40602875005)
passed original U-Boot host API tests, three-ISA target compilation,
actual ARM64/RV64 QEMU physical VirtIO-MMIO disks, Linux ELF ext4
write/fsync/reopen, strict `e2fsck -fn` and host `debugfs` persisted data.
The original two upstream source blobs match the pinned Git object hashes.

**Remaining source ownership:** the NaSeed split-queue lifecycle and DMA
bounce adapter, PCI transport discovery and native block/VFS mounting glue
are still first-party code. The U-Boot driver is upstream original; the
complete storage stack is NOT claimed to be 100% upstream unmodified.
