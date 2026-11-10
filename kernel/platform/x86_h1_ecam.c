#include "x86_h1_ecam.h"
#include "x86_h0_pci.h"
#include "pci_ecam.h"
#include "../arch/x86_64/mmu.h"
#include "../drivers/serial.h"
#include "../errno.h"
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
    nas_print("[H1 ECAM VERIFIED] CF8/ECAM identical functions=");
    nas_dec(h1.verified);
    nas_print(" mapped RO/UC pages=");
    nas_dec(h1.mapped);
    nas_print(" no BAR/DMA/IRQ access\n");
    return 0;
}
