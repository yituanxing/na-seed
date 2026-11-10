# H2: read-only PCI BAR/capability resource inventory

H2 extends the tested H0/H1 physical-hardware bring-up gates in NaSeed Draft
PR #150. It is **not** an NVMe driver, not a BAR aperture sizing operation,
and not a Raspberry Pi or physical PC PASS.

## Reproducible acceptance

```sh
make hw-h2-host-check
make hw-h2-kernel
make hw-h2-iso
```

The H2 CI workflow boots `build/h2-hardware/naseed.iso` on QEMU Q35/OVMF
with `NAS_QEMU_H2_NVME=1`. That setting adds a PCI NVMe **controller only**,
without a namespace, disk image or host disk. It should observe an NVMe
class `01:08:02` candidate, validate H1 CF8/ECAM configuration equivalence,
report BAR base/type and conventional capabilities, and halt at H0's bounded
stop marker. The workflow stores the ISO and complete serial log.

## Resource maturity and safety

- A PCI BAR configuration register supplies a firmware-assigned base,
  space kind, width and prefetchability. It does **not** supply its aperture
  length; reading a BAR must never be treated as sufficient to map it.
- No all-ones BAR sizing writes, PCI Command changes, IRQ routing, BAR MMIO
  reads, NVMe doorbell accesses, DMA or driver binding are performed in H2.
- Legacy capabilities are traversed only when PCI Status reports a list;
  all pointers are 4-byte aligned within 0x40..0xfc, visited at most 32
  times with loop detection. PCIe extended capabilities are deliberately
  deferred until a dedicated bounds-checked 4KiB reader is required.
- Resource snapshots remain diagnostics. The existing `nas_bus_device`
  registry requires a stable 256-byte config snapshot and *qualified BAR
  mapping*. Never weaken its resource checks or invent a second registry.
- QEMU-only coherent DMA cannot be used for unqualified physical controllers.

## Next gate: hardware initialization is a separate operation

Even NVMe Identify, although it only reads controller/namespace metadata,
requires **hardware writes**: PCI bus mastering, MMIO CC/AQA/ASQ/ACQ,
doorbells and DMA queue ownership. H3 must build a properly qualified BAR,
DMA and timeout provider in an isolated QEMU profile first. A real NVMe
driver must use the existing `nas_dma_provider` and `nas_port_block`, and
its external source/license must pass review. No real disks or partitions
are exposed to H2.
