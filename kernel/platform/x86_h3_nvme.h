#ifndef NASEED_X86_H3_NVME_H
#define NASEED_X86_H3_NVME_H

#include "../drivers/core/pci_scan.h"
#include "../drivers/core/pci_resources.h"

/* Explicit QEMU-only admin Identify diagnostic. This operation writes PCI
 * command, NVMe registers and DMA. NEVER run this profile on physical PCs. */
int nas_x86_h3_nvme_identify(const struct nas_pci_function_info *,
                            const struct nas_pci_resources *);

#endif
