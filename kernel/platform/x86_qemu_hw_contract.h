#ifndef NASEED_X86_QEMU_HW_CONTRACT_H
#define NASEED_X86_QEMU_HW_CONTRACT_H

#include "x86_h11_bar.h"
#include "../arch/x86_64/mmu.h"
#include "../errno.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* QEMU-only H11-H15 hardware acceptance bridge. These helpers are restricted to the
 * explicitly qualified Q35 acceptance profiles, not arbitrary physical
 * devices or general interrupt/DMA safety. A future platform PCI resource owner should replace this fixture
 * specific code without changing xHCI class-driver semantics. */
static inline bool nas_x86_qemu_fw_cfg_signature(void) {
    static const char expected[4] = {'Q', 'E', 'M', 'U'};
    const uint16_t select_port = 0x510U, data_port = 0x511U;
    __asm__ volatile("outw %0,%1" :: "a"((uint16_t)0), "Nd"(select_port)
                     : "memory");
    for (unsigned i = 0; i < sizeof(expected); ++i) {
        uint8_t ch = 0;
        __asm__ volatile("inb %1,%0" : "=a"(ch) : "Nd"(data_port)
                         : "memory");
        if (ch != (uint8_t)expected[i]) return false;
    }
    return true;
}

/* CF8/CFC mechanism #1. Call only with the caller's existing IRQ exclusion.
 * Status bits in PCI config DWORD 0x04 are W1C; the upper 16 bits of this
 * write must be zero, never a copied Status halfword. */
static inline void nas_x86_qemu_pci_command_write(uint8_t bus, uint8_t slot,
                                                   uint8_t function,
                                                   uint16_t command) {
    const uint32_t address = UINT32_C(0x80000000) | ((uint32_t)bus << 16) |
        ((uint32_t)slot << 11) | ((uint32_t)function << 8) | 4U;
    const uint16_t select_port = 0xcf8U, data_port = 0xcfcU;
    __asm__ volatile("outl %0,%1" :: "a"(address), "Nd"(select_port)
                     : "memory");
    __asm__ volatile("outl %0,%1" :: "a"((uint32_t)command), "Nd"(data_port)
                     : "memory");
}


/* H11-H15 share the same Q35 controller qualification and 16 KiB
 * MMIO aperture. Class-specific sessions and acceptance markers stay in
 * their callers; this is not a generic PCI or USB discovery policy. */
static inline bool nas_x86_qemu_xhci_function_qualified(
    const struct nas_pci_function_info *fn) {
    return fn && fn->bus==0U && fn->slot==4U && fn->function==0U &&
        fn->vendor_id==0x1b36U && fn->device_id==0x000dU &&
        fn->class_code==0x0cU && fn->subclass==0x03U &&
        fn->programming_interface==0x30U && nas_x86_qemu_fw_cfg_signature();
}

static inline const struct nas_pci_bar_desc *nas_x86_qemu_xhci_bar0(
    const struct nas_pci_resources *resources) {
    if(resources)
        for(unsigned i=0;i<resources->bar_count;i++)
            if(resources->bars[i].index==0U)return &resources->bars[i];
    return NULL;
}

struct nas_x86_qemu_xhci_mmio { uint64_t base; };

static inline int nas_x86_qemu_xhci_read32(void *ctx,uint32_t offset,
                                           uint32_t *value) {
    const uint64_t base=ctx?((const struct nas_x86_qemu_xhci_mmio *)ctx)->base:0;
    if(!base || !value || (offset&3U) ||
       offset>NAS_H11_XHCI_MMIO_BYTES-4U)return -EINVAL;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    *value=*(volatile const uint32_t *)(uintptr_t)(base+offset);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return 0;
}

static inline int nas_x86_qemu_xhci_write32(void *ctx,uint32_t offset,
                                            uint32_t value) {
    const uint64_t base=ctx?((const struct nas_x86_qemu_xhci_mmio *)ctx)->base:0;
    if(!base || (offset&3U) ||
       offset>NAS_H11_XHCI_MMIO_BYTES-4U)return -EINVAL;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    *(volatile uint32_t *)(uintptr_t)(base+offset)=value;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return 0;
}

static inline void nas_x86_qemu_restore_if(uint64_t flags) {
    if(flags&(UINT64_C(1)<<9))__asm__ volatile("sti":::"memory");
}

/* Success hands the caller a live pinned BAR with interrupts disabled;
 * caller restores IF after its controller/class open. On every failure IF
 * is restored; partially installed UC/NX mappings remain pinned, exactly
 * as in the original isolated profiles. No DMA or PCI command is enabled here. */
static inline int nas_x86_qemu_xhci_map(
    struct nas_pci_mmio_owner *owner,const struct nas_pci_resources *resources,
    const struct nas_pci_bar_desc *bar,uint64_t virt,
    struct nas_pci_mmio_claim *claim,uint64_t *flags) {
    *owner=(struct nas_pci_mmio_owner){
        .window_base=NAS_H11_QEMU_MMIO_BASE,
        .window_bytes=NAS_H11_QEMU_MMIO_BYTES
    };
    int rc=nas_pci_mmio_claim_bar(owner,resources,0U,
                                  NAS_H11_XHCI_MMIO_BYTES,claim);
    if(rc)return rc;
    uint64_t saved=0;
    __asm__ volatile("pushfq; popq %0":"=r"(saved)::"memory");
    __asm__ volatile("cli":::"memory");
    *flags=saved;
    rc=nas_pci_mmio_pin_mapping_attempt(owner,claim);
    if(rc) {
        (void)nas_pci_mmio_release(owner,claim);
        nas_x86_qemu_restore_if(saved);
        return rc;
    }
    for(unsigned i=0;i<NAS_H11_XHCI_MMIO_BYTES/4096U;i++) {
        if(!nas_arch_x86_map_device_page(bar->base+(uint64_t)i*4096U,
                                       virt+(uint64_t)i*4096U)) {
            nas_x86_qemu_restore_if(saved);
            return -EIO;
        }
    }
    return 0;
}

#endif
