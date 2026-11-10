#ifndef NASEED_X86_H11_QEMU_XHCI_BAR_H
#define NASEED_X86_H11_QEMU_XHCI_BAR_H

#include "../drivers/core/pci_resources.h"

/* Q35's explicitly enabled 64-bit PCI memory hole, as observed in the
 * genuine H11 QEMU+OVMF firmware enumeration:
 * PCI 1b36:000d 00:04.0 BAR0=0x000000c000000000, type MEM64.
 * This is a narrow 16MiB QEMU-only acceptance window, NOT unrestricted
 * arbitrary-high physical MMIO. The platform independently requires exact
 * QEMU fw_cfg signature and the PCI vendor/device/BDF/class tuple.
 */
#define NAS_H11_XHCI_MMIO_BYTES UINT64_C(0x4000)
#define NAS_H11_QEMU_MMIO_BASE UINT64_C(0x000000c000000000)
#define NAS_H11_QEMU_MMIO_BYTES UINT64_C(0x01000000)
#define NAS_H11_QEMU_MMIO_END \
    (NAS_H11_QEMU_MMIO_BASE + NAS_H11_QEMU_MMIO_BYTES)
/* x86-64 leaf physical-address mask used by arch/x86_64/mmu.c. */
#define NAS_H11_X86_PHYS_LAST UINT64_C(0x000ffffffffff000)

static inline bool nas_h11_qemu_xhci_bar_qualified(
    const struct nas_pci_bar_desc *bar) {
    return bar && bar->index == 0U &&
           bar->kind == NAS_PCI_BAR_MEM64 &&
           !bar->prefetchable &&
           !(bar->base & 4095U) &&
           bar->base >= NAS_H11_QEMU_MMIO_BASE &&
           bar->base <= NAS_H11_QEMU_MMIO_END - NAS_H11_XHCI_MMIO_BYTES &&
           bar->base <= NAS_H11_X86_PHYS_LAST -
                        (NAS_H11_XHCI_MMIO_BYTES - 4096U);
}

#endif
