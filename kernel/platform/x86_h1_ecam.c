#include "x86_h1_ecam.h"
#include "x86_h0_pci.h"
#include "pci_ecam.h"
#include "../arch/x86_64/mmu.h"
#include "../drivers/serial.h"
#include "../errno.h"
#ifdef NAS_ENABLE_H2_PCI_RESOURCE_INVENTORY
#include "../drivers/core/pci_resources.h"
#endif
#ifdef NAS_ENABLE_H3_QEMU_NVME_IDENTIFY
#include "x86_h3_nvme.h"
#endif
#include <stdbool.h>
#include <stddef.h>

#define H1_MAX_CONFIG_PAGES 64U
#define H1_CONFIG_VIRT_BASE UINT64_C(0xfffffe8001000000)
#define H1_PAGE_BYTES 4096U

struct h1_page {
    uint64_t physical;
    uint64_t virtual_address;
};
struct h1_context {
    nas_x86_h1_mmio_allowed allowed;
    void *allowed_context;
    struct h1_page pages[H1_MAX_CONFIG_PAGES];
    unsigned mapped;
    unsigned verified;
#ifdef NAS_ENABLE_H2_PCI_RESOURCE_INVENTORY
    unsigned h2_nvme;
    unsigned h2_ahci;
    unsigned h2_xhci;
#endif
#ifdef NAS_ENABLE_H3_QEMU_NVME_IDENTIFY
    bool h3_candidate_found;
    struct nas_pci_function_info h3_candidate;
    struct nas_pci_resources h3_candidate_resources;
#endif
    struct nas_pci_ecam_host host;
};

/* Lazily map one PCI function's 4K config page at a time; a failed
 * mapping NEVER falls back to a WB HHDM pointer or accesses that device. */
static int h1_mmio_read32(void *context, uint64_t physical, uint32_t *value) {
    struct h1_context *h1 = context;
    uint64_t page = physical & ~UINT64_C(4095);
    uint64_t virtual_address = 0;
    if (!h1 || !value || (physical & 3U) ||
        !h1->allowed(h1->allowed_context,page,H1_PAGE_BYTES)) return -EIO;
    for(unsigned i=0;i<h1->mapped;i++) {
        if(h1->pages[i].physical==page) {
            virtual_address=h1->pages[i].virtual_address;
            break;
        }
    }
    if(!virtual_address) {
        if(h1->mapped==H1_MAX_CONFIG_PAGES) return -ENOSPC;
        virtual_address=H1_CONFIG_VIRT_BASE+
                        (uint64_t)h1->mapped*H1_PAGE_BYTES;
        if(!nas_arch_x86_map_readonly_device_page(page,virtual_address))
            return -EIO;
        h1->pages[h1->mapped].physical=page;
        h1->pages[h1->mapped].virtual_address=virtual_address;
        ++h1->mapped;
    }
    const volatile uint32_t *mapped=(const volatile uint32_t *)(uintptr_t)
                                     (virtual_address+(physical&4095U));
    *value=*mapped;
    return 0;
}

static int h1_compare_function(void *context,
                               const struct nas_pci_function_info *function) {
    struct h1_context *h1=context;
    uint32_t identity=nas_pci_ecam_scan_read32(&h1->host,function->bus,
                                                function->slot,
                                                function->function,0U);
    uint32_t class_reg=nas_pci_ecam_scan_read32(&h1->host,function->bus,
                                                 function->slot,
                                                 function->function,8U);
    if(h1->host.read_faults) return -EIO;
    if ((uint16_t)identity!=function->vendor_id ||
        (uint16_t)(identity>>16)!=function->device_id ||
        (uint8_t)(class_reg>>24)!=function->class_code ||
        (uint8_t)(class_reg>>16)!=function->subclass ||
        (uint8_t)(class_reg>>8)!=function->programming_interface)
        return -EIO;
#ifdef NAS_ENABLE_H2_PCI_RESOURCE_INVENTORY
    /* Resource metadata is NOT a mapped BAR or a driver-ready bus object.
     * The H2 kernel never probes apertures, mutates PCI Command or binds. */
    const struct nas_pci_config_reader ecam_reader={
        &h1->host,nas_pci_ecam_scan_read32
    };
    struct nas_pci_resources resources={0};
    if(nas_pci_resources_probe(&ecam_reader,function,&resources) ||
       h1->host.read_faults)return -EIO;
    nas_print("[H2 PCI RESOURCE] bus=");nas_dec(function->bus);
    nas_print(" slot=");nas_dec(function->slot);
    nas_print(" function=");nas_dec(function->function);
    nas_print(" BAR metadata=");nas_dec(resources.bar_count);
    nas_print(" caps=");nas_dec(resources.cap_count);
    nas_print(" unqualified (no driver bind)\n");
    for(unsigned i=0;i<resources.bar_count;i++) {
        const struct nas_pci_bar_desc *bar=&resources.bars[i];
        nas_print("[H2 BAR] slot=");nas_dec(bar->index);
        nas_print(" kind=");nas_dec(bar->kind);
        nas_print(" base=");nas_hex(bar->base);
        nas_print(" prefetch=");nas_dec(bar->prefetchable);
        nas_print(" size=UNKNOWN\n");
    }
    if(function->class_code==0x01U && function->subclass==0x08U &&
       function->programming_interface==0x02U) {
#ifdef NAS_ENABLE_H3_QEMU_NVME_IDENTIFY
#ifdef NAS_ENABLE_H4_QEMU_NVME_READONLY
        nas_print("[H2 NVME CANDIDATE] discovered; H4 QEMU namespace read follows\n");
#else
        nas_print("[H2 NVME CANDIDATE] discovered; H3 QEMU admin test follows\n");
#endif
#else
        nas_print("[H2 NVME CANDIDATE] discovered; no controller writes/DMA\n");
#endif
        ++h1->h2_nvme;
#ifdef NAS_ENABLE_H3_QEMU_NVME_IDENTIFY
        /* Keep H2's discovery phase read-only until the entire PCI scan
         * confirms there is exactly one NVMe candidate. In particular a
         * second controller must not appear *after* controller writes. */
        if(!h1->h3_candidate_found) {
            h1->h3_candidate=*function;
            h1->h3_candidate_resources=resources;
            h1->h3_candidate_found=true;
        }
#endif
    } else if(function->class_code==0x01U && function->subclass==0x06U &&
              function->programming_interface==0x01U) ++h1->h2_ahci;
    else if(function->class_code==0x0cU && function->subclass==0x03U &&
            function->programming_interface==0x30U) ++h1->h2_xhci;
#endif
    ++h1->verified;
    return 0;
}

int nas_x86_h1_ecam_verify(const struct nas_acpi_discovery *acpi,
                            nas_x86_h1_mmio_allowed allowed, void *context) {
    if(!acpi || !allowed) return -EINVAL;
    const struct nas_acpi_mcfg_window *window=NULL;
    for(unsigned i=0;i<acpi->mcfg_count;i++) {
        if(acpi->mcfg[i].segment==0U && acpi->mcfg[i].bus_start==0U) {
            window=&acpi->mcfg[i];
            break;
        }
    }
    if(!window) {
        nas_print("[H1 ECAM] no validated segment-0 bus-0 MCFG window\n");
        return -ENODEV;
    }
    struct h1_context h1={0};
    h1.allowed=allowed;
    h1.allowed_context=context;
    h1.host=(struct nas_pci_ecam_host){
        .window=window,.segment=0U,.context=&h1,.read32=h1_mmio_read32
    };
    const struct nas_pci_config_reader cf8={
        NULL,nas_x86_h0_pci_config_read32
    };
    struct nas_pci_scan_stats stats={0};
    int result=nas_pci_scan_root(&cf8,0U,h1_compare_function,&h1,&stats);
    if(result || h1.host.read_faults || !h1.verified) {
        nas_print("[H1 ECAM] fail-closed: mapping, config or CF8 mismatch\n");
        return result ? result : -EIO;
    }
#ifdef NAS_ENABLE_H3_QEMU_NVME_IDENTIFY
    /* H6 negative acceptance: the complete *read-only* scan must refuse
     * ambiguous NVMe ownership before calling any write-capable H3/H4 core.
     * CF8 SELECT writes during discovery are not controller-register writes.
     * A dedicated result distinguishes this intentionally refused QEMU image
     * from an ordinary probe/controller failure in the boot harness. */
#ifdef NAS_ENABLE_H6_QEMU_AMBIGUOUS_NVME
    if(h1.h2_nvme==2U && h1.h3_candidate_found) {
        nas_print("[H6 PCI FAIL-CLOSED] duplicate NVMe candidates; controller writes=0 DMA=0\n");
        return -EEXIST;
    }
#endif
    if(h1.h2_nvme!=1U || !h1.h3_candidate_found ||
       nas_x86_h3_nvme_identify(&h1.h3_candidate,
                                 &h1.h3_candidate_resources)) {
        nas_print("[H3 NVME FAIL] one qualified candidate required before controller writes\n");
        return -ENODEV;
    }
#endif
    nas_print("[H1 ECAM VERIFIED] CF8/ECAM identical functions=");
    nas_dec(h1.verified);
    nas_print(" mapped RO/UC pages=");
    nas_dec(h1.mapped);
#ifdef NAS_ENABLE_H3_QEMU_NVME_IDENTIFY
#ifdef NAS_ENABLE_H4_QEMU_NVME_READONLY
    nas_print(" (H1 config snapshot; H4 QEMU namespace BAR MMIO separate)\n");
#else
    nas_print(" (H1 config snapshot; H3 QEMU admin BAR MMIO separate)\n");
#endif
#else
    nas_print(" no BAR MMIO/DMA/IRQ access\n");
#endif
#ifdef NAS_ENABLE_H2_PCI_RESOURCE_INVENTORY
    nas_print("[H2 PCI RESOURCE VERIFIED] NVMe=");nas_dec(h1.h2_nvme);
    nas_print(" AHCI=");nas_dec(h1.h2_ahci);
    nas_print(" xHCI=");nas_dec(h1.h2_xhci);
#ifdef NAS_ENABLE_H3_QEMU_NVME_IDENTIFY
#ifdef NAS_ENABLE_H4_QEMU_NVME_READONLY
    nas_print(" (H2 discovery snapshot; H4 QEMU namespace writes separate)\n");
#else
    nas_print(" (H2 discovery snapshot; H3 QEMU admin writes separate)\n");
#endif
#else
    nas_print(" PCI register writes=0 driver registrations=0\n");
#endif
#endif
    return 0;
}
