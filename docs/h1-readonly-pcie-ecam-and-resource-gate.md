# H1: read-only PCIe ECAM and resource qualification

H1 extends the tested H0 firmware/PCI inventory in Draft PR #150. This is a
physical-hardware *discovery* milestone, not a PCI driver or a physical boot
PASS. Normal x86-64, RV64 and AArch64 outputs remain unchanged.

## Build and evidence

```sh
make hw-h1-host-check
make hw-h1-kernel
make hw-h1-iso
```

Output is isolated at `build/h1-hardware/naseed.iso`. The H1 GitHub workflow
must prove an actual QEMU Q35/OVMF boot reaches:

```text
[H0 ACPI] XSDT=...
[H0 PCI] scanned buses=...
[H1 ECAM VERIFIED] CF8/ECAM identical functions=...
[H0 COMPLETE] firmware inventory only; no physical disk I/O
```

The workflow preserves the serial log and ISO. QEMU evidence cannot be called
real-hardware PASS; physical PC verification still needs a spare machine,
model, firmware, serial/screen capture, boot commit and no writable disks.

## Safety and architectural boundaries

1. MCFG `ecam_base` addresses **bus zero** of its segment, even when
   `bus_start` is not zero (Linux PCI ACPI host-bridge documentation). The
   permitted bus range is an access bound, not a subtraction term. ACPI
   parser and ECAM host tests reject arithmetic overflow and bad ranges.
2. ACPI table pages use the separately validated Limine ACPI table mapping.
   PCIe ECAM pages are mapped only from reserved firmware MMIO descriptors,
   using new supervisor-only, read-only, UC, NX PTEs. An absent descriptor,
   mapping collision or MMIO read failure fails closed; no HHDM MMIO alias.
3. H1 uses CF8 as an independent reference and compares only the discovered
   vendor, device and class DWORDs against ECAM. Config writes, BAR reads,
   BAR sizing, bus mastering, IRQ configuration and DMA are forbidden.
4. The existing `nas_bus_device` registry intentionally rejects an unowned
   PCI resource without a properly mapped BAR and config snapshot. H1 does
   **not** relax that rule or register discovered devices prematurely.
   Only a future platform resource provider may qualify BAR/IRQ/DMA and then
   publish to `nas_bus_device`; class drivers continue to publish exclusively
   through `nas_port_block` / `nas_netdev` / the existing input layer.
5. Physical DMA must not reuse the QEMU-only coherent identity mapping.
   Actual NVMe/AHCI/xHCI sources need manifest and license review; NaOS GPLv3
   history and U-Boot GPL-2.0+ modules are design references until resolved.

## Next true-hardware gate

On a spare PC, boot the H1 image with no writable disk exposed. Retain the
full serial/screen output even if ECAM is unavailable. Then build an explicit
PCI host bridge and resource owner (BAR mapping, IOMMU/DMA, IOAPIC/MSI).
The first real storage driver acceptance should be **NVMe Identify** and
read-only namespace reads against a disposable device, with independent
readback evidence, before enabling any write path.
