#ifndef NASEED_X86_H0_PCI_H
#define NASEED_X86_H0_PCI_H

#include "../drivers/core/pci_scan.h"

/* EARLY BOOT ONLY, with interrupts disabled and before any PCI class driver
 * runs. CF8/CFC reads see segment 0's conventional 256-byte config space;
 * ACPI/MCFG and PCIe ECAM remain independently validated resource providers.
 * No BAR probing, bus mastering or driver activation takes place. */
uint32_t nas_x86_h0_pci_config_read32(void *context, uint8_t bus,
                                       uint8_t slot, uint8_t function,
                                       uint16_t offset);
int nas_x86_h0_pci_inventory(struct nas_pci_scan_stats *stats);

#endif
