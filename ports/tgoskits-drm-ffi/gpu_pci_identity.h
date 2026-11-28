/* SPDX-License-Identifier: MIT
 * Read-only, owner-derived PCI identity for the Linux sysfs DRM projection.
 * NaSeed's existing bus enumerator remains the sole source of hardware data.
 */
#ifndef NAS_TGOS_GPU_PCI_IDENTITY_H
#define NAS_TGOS_GPU_PCI_IDENTITY_H
#include <stdint.h>
struct nas_tgos_gpu_pci_identity {
    uint32_t domain,bus,slot,function;
    uint32_t vendor,device,subvendor,subdevice,revision;
};
int nas_tgos_original_gpu_pci_identity(struct nas_tgos_gpu_pci_identity *out);
#endif
