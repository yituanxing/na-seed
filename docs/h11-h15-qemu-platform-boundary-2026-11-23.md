# H11–H15 QEMU PCI access consolidation (2026-11-23)

This is a **source refactor**, not a new hardware milestone or real-PC
qualification. Based on the exact H15 hardware branch head
`ed3ea2df4794aa6b77e31466428cd5f166f1397b`.

## Verified starting architecture

- The shared `kernel/drivers/core/bus.[ch]` has typed bus/device/driver
  matching and pending-removal handling; it is **not yet the owner of the
  H11–H15 hardware acceptance lifecycle**.
- The shared `pci_resources.[ch]` implements bounded BAR claims, mapping
  pinning and quiesced release. `port/dma.[ch]` implements bounded DMA leases.
- H11–H15 each still own a specialized platform bootstrap, QEMU check,
  direct CF8/CFC Command write, xHCI I/O trampoline, BAR map and teardown.
  Those independent test paths intentionally select different USB device
  topologies and must remain independently reproducible.

## This change

Move the **identical QEMU fw_cfg signature read** and **PCI Command low-16
write** from five H11–H15 platform fixtures into one explicit, x86-specific
header `x86_qemu_hw_contract.h`. Keep callers' interrupt exclusion,
controller identity matching, safe BAR/lease ownership, workflow topology,
negative tests and shutdown order unchanged. Zero the W1C Status halfword
when writing config DWORD 0x04. Do not claim that a header helper converts
the present xHCI acceptance code into a generic physical-hardware driver.

## Next integration slices (not implemented here)

1. Introduce one **platform-owned PCI resource handle** coupling qualified
   BDF, BAR claim, UC/NX map, original PCI Command and verified quiesced
   unmap/restore; explicit fail-closed partial-map state.
2. Attach PCI device identities through the existing `nas_bus_device` and
   `nas_bus_driver` model. Preserve xHCI, NVMe, AHCI and E1000 class
   interfaces; **do not introduce a second bus registry**.
3. Move xHCI root-port device/topology discovery out of H11–H15
   special-case platform policy, while retaining independent MSC, HID and
   FTDI class-device registration and release. Do not claim generic hubs,
   hotplug, MSI or physical IOMMU before separate hardware acceptance.
4. Route block, input, character and network publication through their
   existing core-device contracts. Measure new driver glue lines, duplicate
   platform ownership, complete rollback paths, and upstream diff surface.
5. Execute the existing H0–H15 **exact-head QEMU and negative-control**
   suite, historical VirtIO-Blk runtime program gates, and three-ISA
   userspace regressions. An unrun gate is not PASS.

## Verification boundary

Standalone freestanding x86_64 C syntax proof for the shared header is
possible without executing privileged I/O. The previous hardware parent
`ed3ea2df` passed its H0–H15 GitHub Actions suite. Those historical
results do **not** qualify this new refactor head; check a fresh exact-head
run before moving it into the stacked hardware PR.
