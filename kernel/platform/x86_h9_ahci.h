#ifndef NASEED_X86_H9_AHCI_H
#define NASEED_X86_H9_AHCI_H

#include "../drivers/core/pci_resources.h"

/* The H9 driver is enabled only by the isolated QEMU q35 SATA profile.
 * PCI inventory has already completed without controller writes or DMA. */
int nas_x86_h9_ahci_bind(const struct nas_pci_function_info *,
                         const struct nas_pci_resources *);
int nas_x86_h9_ahci_shutdown(void);

#endif
