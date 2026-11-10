#ifndef NASEED_X86_H10_E1000_H
#define NASEED_X86_H10_E1000_H

#include "../drivers/core/pci_resources.h"
#include <stdbool.h>
#include <stdint.h>

/* Isolated QEMU q35 classic Intel 82540EM (8086:100e), not the default
 * QEMU 82574L/e1000e function. Discovery is complete before bind mutates
 * its owned PCI Command, BAR MMIO or Ethernet DMA registers. */
int nas_x86_h10_e1000_bind(const struct nas_pci_function_info *,
                          const struct nas_pci_resources *);

/* Pure preflight for host regressions: BAR0 is a QEMU-only MMIO mapping
 * candidate, not authority to touch physical hardware. Real bind also
 * checks fw_cfg and the completed unique-function PCI scan. */
static inline bool nas_x86_h10_e1000_candidate(
    const struct nas_pci_function_info *function,
    const struct nas_pci_resources *resources) {
    if(!function || !resources ||
       resources->bar_count>NAS_PCI_RESOURCE_BAR_LIMIT ||
       function->bus!=0U ||
       function->slot!=4U || function->function!=0U ||
       function->vendor_id!=0x8086U || function->device_id!=0x100eU ||
       function->class_code!=2U || function->subclass!=0U)return false;
    for(unsigned i=0;i<resources->bar_count;i++) {
        const struct nas_pci_bar_desc *bar=&resources->bars[i];
        if(bar->index!=0U)continue;
        return bar->kind==NAS_PCI_BAR_MEM32 && !bar->prefetchable &&
               !(bar->base&4095U) &&
               bar->base>=UINT64_C(0x81000000) &&
               bar->base<=UINT64_C(0x81fe0000);
    }
    return false;
}

/* Returns -EBUSY until the upstream lwIP worker drops its final pinned
 * device reference. The deferred PID1 shutdown retries without freeing
 * DMA or the BAR beneath a live network consumer. */
int nas_x86_h10_e1000_shutdown(void);

#endif
