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

NaOS's inspected kernel repository top-level LICENSE is **GPLv3**. Its block
and queue sources carry copyright notices but no separate per-file GPLv2
grant in the inspected source. NaSeed's pinned lwext4
`ext4_xattr.c`/`ext4_extent.c` explicitly permit GPLv2 **or later**;
the old claim that these files were GPLv2-only and necessarily incompatible
with GPLv3 was incorrect. This does **not** authorize copying NaOS C code:
check the actual source grants, project-wide license choice and required
attribution before importing any NaOS implementation. The current port
references NaOS's architecture but contains no copied NaOS driver source.

## Current physical-QEMU evidence (2026-10-30, `221c46a`)

The source-preserving U-Boot request engine now executes through **both**
modern transports: AArch64/RV64 VirtIO-MMIO and x86-64 QEMU q35 VirtIO-PCI.
The dedicated x86-64 [physical ext4 run #40606858869](https://github.com/yituanxing/na-seed/actions/runs/40606858869)
passed the actual PCI BAR/capability path, real ring3 Linux ELF
write/fsync/reopen, clean guest ext4 unmount/flush, strict host e2fsck,
and persisted-content verification via debugfs. This proves an independent
QEMU raw disk, not a Limine module or physical-host deployment. The x86
MSI-X capability-length parsing issue described below was fixed before it.

Sections below preserve the intermediate history. Earlier "not yet PCI
hardware acceptance" and "pending new exact-head" statements are **not**
current blockers. Still excluded are IRQ-completion/waitqueue, concurrent
requests, hotplug, general PCIe topology, IOMMU and physical hardware;
final combined-source distribution-license review remains a separate
**release** gate (`docs/distribution-license-review.md`).

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

## Completion integrity audit (next exact-head gate)

The unchanged U-Boot request code initializes `type` and `sector` of
`virtio_blk_outhdr` but not its `ioprio` member. The NaSeed adapter
canonicalizes `ioprio=0` before handing any header to DMA. Only IN/OUT/FLUSH
are accepted by the bounded adapter, so WRITE_ZEROES is not negotiated even
when the underlying device advertises it. After polling, an OK read must
account for the full payload plus the status byte in the used-ring length;
OK writes and flushes must report exactly the status byte. A short successful
read, oversized successful write, or timeout poisons/resets the transport and
returns EIO, never stale filesystem contents. A failed read may write only a
status byte. Host emulation exercises these malformed completions without
editing pinned upstream sources. This is not proof of IRQ-driven concurrency,
PCI hardware discovery, or general-purpose physical-device readiness.

## Modern PCI transport implementation (host-first)

Modern PCI capability parsing and common/notify/device BAR operations now
have a **separate transport backend**, not a new block request engine:
`kernel/drivers/virtio/pci.[ch]`. The platform must supply a 256-byte PCI
configuration snapshot and bounds-checked, mapped MMIO BAR resources. The
common VirtIO ops are shared with MMIO and can bind the exact same original
U-Boot block driver. It validates capability chains (including loops),
required capabilities, BAR ranges, queue size/enable, 64-bit DMA addresses,
and the device-provided queue notification multiplier. Host mock checks
cover malformed PCI capabilities and queue control; cross-ISA freestanding
syntax checks enforce an architecture-independent source.

**Historical host-first checkpoint:** x86 PCI enumeration, BAR mapping and
real QEMU disk persistence had not yet been connected and run at this stage.
The later run #40606858869 passed that gate. PCI still does not promise
IRQ completion or concurrent requests.

## x86-64 PCI physical-device gate

New opt-in root-bus QEMU q35 PCI mechanism-1 discovery maps real BAR MMIO
through the same supervisor-only, uncached, NX PTE helper as x86 LAPIC. BAR
size probes disable memory decode before restoring firmware-assigned values;
no BAR allocations or ISA-specific VirtIO requests are introduced. The PCI
capability transport binds to the same U-Boot block bus driver, split queue,
DMA and BlockDevice used by ARM/RV MMIO. This is an intentionally bounded
**QEMU root bus 0** implementation, not a claim of general ACPI/PCIe bridges,
IOMMU, MSI-X, hotplug or native physical hardware coverage.

`scripts/probe-lwext4-virtio-pci-x86.sh` supplies a separate raw ext4 PCI disk
(not a Limine module), runs unmodified native Linux ELF ext4 write/fsync,
and requires clean guest unmount + host e2fsck/debugfs persistence. This is
release-gating until the exact-head CI proves it; neither the new code nor
syntax checks alone establish a passing hardware acceptance.

### QEMU q35 PCI capability discriminator correction

Real x86 QEMU root-bus acceptance on exact head `26676c5a` found
`[VIRTIO PCI FAIL] stage=caps error=22` after successfully mapping BAR1
(4 KiB) and BAR4 (16 KiB). Root-cause audit: PCI capabilities are a linked
list with **two universal bytes** (ID/next); the third byte is a length
*only* for vendor-specific ID 0x09. For a standard MSI-X capability, byte
+2 is Message Control and can be zero. The original parser erroneously
validated that byte as a length for every capability, rejecting the valid
QEMU device. The corrected parser checks the vendor-specific length only
for cap ID 0x09, and the host regression explicitly places an MSI-X cap
with a zero Message Control ahead of the same real VirtIO caps. This is a
source-level fix pending physical QEMU acceptance; later dedicated run
#40606858869 passed that gate.
