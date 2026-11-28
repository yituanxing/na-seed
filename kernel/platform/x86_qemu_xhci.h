#ifndef NASEED_X86_QEMU_XHCI_H
#define NASEED_X86_QEMU_XHCI_H
#include "x86_qemu_hw_contract.h"
#include "../drivers/usb/xhci.h"
#include "../drivers/serial.h"

/* One resource lifetime per isolated QEMU xHC profile. Class publication
 * and DMA quiescence remain caller-owned; release is legal ONLY after
 * the respective class/controller close succeeds. */
struct nas_x86_qemu_xhci {
    struct nas_x86_qemu_mmio mmio;
    struct nas_pci_mmio_owner owner;
    struct nas_pci_mmio_claim claim;
    struct nas_xhci_io io;
    bool attempted;
    uint8_t bus,slot,function;
    uint16_t command;
};
static inline int nas_x86_qemu_xhci_prepare(
    struct nas_x86_qemu_xhci *hw,const struct nas_pci_function_info *fn,
    const struct nas_pci_resources *resources,uint64_t *flags,
    const char *function_failure,const char *bar_failure,
    const char *bar_label,const char *bar_suffix) {
    if(!fn || !resources)return -EINVAL;
    if(hw->attempted || !nas_x86_qemu_xhci_function_qualified(fn)) {
        nas_print(function_failure);
        return -ENODEV;
    }
    hw->attempted=true;
    const struct nas_pci_bar_desc *bar=nas_x86_qemu_find_bar(resources,0U);
    if(!nas_h11_qemu_xhci_bar_qualified(bar)) {
        nas_print(bar_failure);
        return -ENODEV;
    }
    struct nas_pci_mmio_claim claim={0};
    int rc=nas_x86_qemu_map_bar(&hw->owner,resources,bar,
        hw->mmio.base,NAS_H11_XHCI_MMIO_BYTES,
        NAS_H11_QEMU_MMIO_BASE,NAS_H11_QEMU_MMIO_BYTES,&claim,flags);
    if(rc)return rc;
    hw->claim=claim;
    hw->bus=fn->bus;
    hw->slot=fn->slot;
    hw->function=fn->function;
    hw->command=resources->command;
    nas_print(bar_label);
    nas_hex(bar->base);
    nas_print(bar_suffix);
    if((resources->command&UINT16_C(6))!=UINT16_C(6))
        nas_x86_qemu_pci_command_write(fn->bus,fn->slot,fn->function,
                                      (uint16_t)(resources->command|6U));
    hw->io=(struct nas_xhci_io){
        .context=&hw->mmio,.mmio_bytes=NAS_H11_XHCI_MMIO_BYTES,
        .read32=nas_x86_qemu_mmio_read32,.write32=nas_x86_qemu_mmio_write32
    };
    return 0; /* Existing IRQ exclusion remains until the caller restores IF. */
}
static inline int nas_x86_qemu_xhci_release(struct nas_x86_qemu_xhci *hw) {
    int rc=nas_pci_mmio_quiesced_unmap_release(&hw->owner,&hw->claim,
        hw->mmio.base,true,nas_arch_x86_unmap_device_pages);
    if(!rc)nas_x86_qemu_pci_command_write(hw->bus,hw->slot,hw->function,hw->command);
    return rc;
}
#endif
