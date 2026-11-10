# H6 QEMU PCI resource lifecycle and ambiguity gate

## Two independent runtime proofs

H5 proved that a QEMU NVMe device moved from PCI `00:03.0` to `00:05.0`
could read LBA0/LBA1 through `nas_block_read`, using an actually relocated
BAR0 address (`0x000000c000004000`). H6 uses that same isolated controller
and known 1 MiB read-only backing fixture to exercise stricter PCI MMIO
ownership while retaining the original NVMe read/block test.

H6 also adds a bounded `nas_dma_lease` to the existing DMA provider:
the NVMe session owns each SQ/CQ/bounce page, rejects foreign-page access,
arms ownership *before* enabling the controller, and blocks page release
until the NVMe `CSTS.RDY=0` quiescence check succeeds. A failed free preserves
ownership so cleanup can be retried. Host tests cover foreign leases,
double ownership, active-DMA release attempts, retry, and the eight-page
capacity limit. This is software lifetime accounting, **not** an IOMMU.

**Positive proof:** `NAS_ENABLE_H6_QEMU_RESOURCE_LIFETIME=1`, isolated UEFI
image `build/h6-qemu-only/naseed.iso`, `NAS_QEMU_H5_PCI_NVME=1` runtime.
Acceptance requires the pinned-MMIO-claim marker and the actual NVMe LBA0/
LBA1 byte match, `nas_block_write=-EROFS`, and unchanged SHA-256. The BAR's
physical address must differ from H4's old fixed location.

**Negative proof:** `NAS_ENABLE_H6_QEMU_AMBIGUOUS_NVME=1`, isolated UEFI
image `build/h6-qemu-ambiguous/naseed.iso`, `NAS_QEMU_H6_TWO_NVME=1` runtime.
Two emulated QEMU NVMe controllers exist at PCI `00:05.0` and `00:06.0`,
**without namespaces or backing disks**. NaSeed completes CF8/ECAM and
PCI BAR/capability metadata discovery, counts two NVMe candidates, and
returns `-EEXIST` before any write-capable NVMe driver entry. The serial
marker explicitly denotes controller MMIO/PCI writes=0, DMA allocations=0;
the QEMU acceptance rejects positive Identify/READ/BAR-pinning markers.
This proves the *NaSeed driver path* stopped before controller writes, not
that UEFI firmware or QEMU itself never wrote PCI configuration registers.

## Run

```sh
make hw-h6-host-check
make hw-h6-iso
make hw-h6-ambiguous-iso
sh scripts/check-h6-pci-mmio-qemu.sh
sh scripts/check-h6-nvme-ambiguity-qemu.sh
```

Separate build output directories, runtime opt-in variables, and mutually
exclusive QEMU controller profiles prevent accidental incremental-object
reuse and ambient block-disk replacement. CI stores both UEFI serial traces.

## Real-hardware boundary

None of this enables physical PCI/NVMe control. H6 is a QEMU-specific
write-capable experiment. A BAR snapshot plus a QEMU high-MMIO range does
not prove physical host-bridge aperture allocation or BAR size. Actual
PCI driver authorization requires independently owned resource ranges,
mapping/unmapping lifecycle, IOMMU/DMA bus-address translation, cache
coherency handling, IRQ policy, timeout recovery and device reset.
Default PC hardware discovery stays in H0–H2 read-only images. No GPL
driver source was copied from Linux or NaOS into this test profile.
