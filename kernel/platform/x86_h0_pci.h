#ifndef NASEED_X86_H0_PCI_H
#define NASEED_X86_H0_PCI_H

#include "../drivers/core/pci_scan.h"

/* EARLY BOOT ONLY, with interrupts disabled and before any PCI class driver
 * runs. CF8/CFC reads see segment 0's conventional 256-byte config space;
 * firmware ACPI/MCFG and PCIe ECAM resource discovery are separate future
 * steps. No BAR probing, bus mastering or driver activation takes place. */
int nas_x86_h0_pci_inventory(struct nas_pci_scan_stats *stats);

#endif
