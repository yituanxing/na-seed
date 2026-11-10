#include "x86_h10_e1000.h"
#include "../arch/x86_64/mmu.h"
#include "../drivers/e1000/readonly.h"
#include "../drivers/port/net.h"
#include "../drivers/serial.h"
#include "../errno.h"
#include <stdbool.h>
#include <stdint.h>

/* The exact H10 QEMU fixture opts out of Q35's default e1000e (10d3) and
 * supplies a CLASSIC e1000 82540EM at 00:04.0. Both identification and
 * BAR ownership are opt-in; a matching class code alone never authorizes
 * DMA on arbitrary PC hardware. */
#define H10_E1000_MMIO_BYTES UINT32_C(0x20000)
#define H10_E1000_MMIO_PAGES (H10_E1000_MMIO_BYTES/4096U)
#define H10_E1000_VIRT_BASE UINT64_C(0xfffffe8001400000)
/* Bound to a fixed, isolated 256 MiB Q35/OVMF experiment. A firmware BAR
 * layout change must be investigated; never widen this from PCI BAR data. */
#define H10_QEMU_PCI_MMIO_BASE UINT64_C(0x81000000)
#define H10_QEMU_PCI_MMIO_BYTES UINT64_C(0x01000000)
#define H10_QEMU_PCI_MMIO_END (H10_QEMU_PCI_MMIO_BASE+H10_QEMU_PCI_MMIO_BYTES)

struct h10_mmio {uint64_t virtual_base;};
static struct h10_mmio h10_mmio={H10_E1000_VIRT_BASE};
static struct nas_e1000_io h10_io;
static struct nas_e1000_session h10_session;
static struct nas_netdev h10_netdev;
static struct nas_pci_mmio_owner h10_owner;
static struct {
    bool attempted,attached,core_live;
    uint8_t bus,slot,function;
    uint16_t original_command;
    struct nas_pci_mmio_claim claim;
} h10_persistent;

static bool h10_is_qemu(void) {
    static const char magic[4]={'Q','E','M','U'};
    const uint16_t selector_port=0x510U,data_port=0x511U;
    const uint16_t signature_select=0U;
    __asm__ volatile("outw %0,%1" :: "a"(signature_select),
                     "Nd"(selector_port) : "memory");
    for(unsigned i=0;i<sizeof(magic);i++) {
        uint8_t value=0;
        __asm__ volatile("inb %1,%0" : "=a"(value) : "Nd"(data_port)
                         : "memory");
        if(value!=(uint8_t)magic[i])return false;
    }
    return true;
}
static void h10_pci_command_write(uint8_t bus,uint8_t slot,uint8_t function,
                                  uint16_t command) {
    const uint32_t address=UINT32_C(0x80000000)|((uint32_t)bus<<16)|
             ((uint32_t)slot<<11)|((uint32_t)function<<8)|4U;
    const uint16_t selector=0xcf8U,data=0xcfcU;
    __asm__ volatile("outl %0,%1" :: "a"(address),"Nd"(selector):"memory");
    /* The status half of this DWORD is W1C. Always write zero above the
     * command low word instead of acknowledging an unrelated PCI error. */
    __asm__ volatile("outl %0,%1" :: "a"((uint32_t)command),"Nd"(data)
                     : "memory");
}
static int h10_read32(void *ctx,uint32_t offset,uint32_t *value) {
    const struct h10_mmio *map=ctx;
    if(!map || !value || (offset&3U) ||
       offset>H10_E1000_MMIO_BYTES-4U)return -EINVAL;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    *value=*(volatile const uint32_t *)(uintptr_t)
             (map->virtual_base+offset);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return 0;
}
static int h10_write32(void *ctx,uint32_t offset,uint32_t value) {
    const struct h10_mmio *map=ctx;
    if(!map || (offset&3U) || offset>H10_E1000_MMIO_BYTES-4U)
        return -EINVAL;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    *(volatile uint32_t *)(uintptr_t)(map->virtual_base+offset)=value;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return 0;
}
static const struct nas_netdev_ops h10_netdev_ops={
    .transmit=nas_e1000_transmit,
    .poll=nas_e1000_poll,
    .link_up=nas_e1000_link_up
};

int nas_x86_h10_e1000_bind(const struct nas_pci_function_info *function,
                           const struct nas_pci_resources *resources) {
    if(!function || !resources)return -EINVAL;
    if(h10_persistent.attempted ||
       !nas_x86_h10_e1000_candidate(function,resources) ||
       !h10_is_qemu()) {
        nas_print("[H10 E1000 FAIL] unqualified QEMU 82540EM function\n");
        return -ENODEV;
    }
    h10_persistent.attempted=true;
    const struct nas_pci_bar_desc *bar=NULL;
    for(unsigned i=0;i<resources->bar_count;i++)
        if(resources->bars[i].index==0U){bar=&resources->bars[i];break;}
    if(!bar || bar->kind!=NAS_PCI_BAR_MEM32 || bar->prefetchable ||
       (bar->base&4095U) || bar->base<H10_QEMU_PCI_MMIO_BASE ||
       bar->base>H10_QEMU_PCI_MMIO_END-H10_E1000_MMIO_BYTES) {
        nas_print("[H10 E1000 FAIL] unqualified QEMU BAR0\n");
        return -ENODEV;
    }
    h10_owner=(struct nas_pci_mmio_owner){
        .window_base=H10_QEMU_PCI_MMIO_BASE,
        .window_bytes=H10_QEMU_PCI_MMIO_BYTES
    };
    struct nas_pci_mmio_claim claim={0};
    int rc=nas_pci_mmio_claim_bar(&h10_owner,resources,0U,
                                  H10_E1000_MMIO_BYTES,&claim);
    if(rc)return rc;
    uint64_t flags=0;
    __asm__ volatile("pushfq; popq %0":"=r"(flags)::"memory");
    __asm__ volatile("cli":::"memory");
    rc=nas_pci_mmio_pin_mapping_attempt(&h10_owner,&claim);
    if(rc) {
        (void)nas_pci_mmio_release(&h10_owner,&claim);
        goto restore;
    }
    /* E1000 registers extend beyond the first 4KiB (RX/TX rings, RAL0);
     * all 32 BAR0 pages are distinct supervisor-only UC/NX mappings.
     * A partial mapping failure leaves the entire claim permanently pinned. */
    for(unsigned i=0;i<H10_E1000_MMIO_PAGES;i++) {
        if(!nas_arch_x86_map_device_page(
             bar->base+(uint64_t)i*4096U,
             H10_E1000_VIRT_BASE+(uint64_t)i*4096U)) {
            rc=-EIO;goto restore;
        }
    }
    h10_persistent.claim=claim;
    h10_persistent.bus=function->bus;
    h10_persistent.slot=function->slot;
    h10_persistent.function=function->function;
    h10_persistent.original_command=resources->command;
    nas_print("[H10 E1000 BAR QUALIFIED] 00:04.0 BAR0=");
    nas_hex(bar->base);
    nas_print(" 128KiB UC/NX QEMU-only\n");
    if((resources->command&UINT16_C(6))!=UINT16_C(6))
        h10_pci_command_write(function->bus,function->slot,
                               function->function,
                               (uint16_t)(resources->command|6U));
    /* H10 first end-to-end network gate is bounded NAPI-style polling.
     * Leave legacy INTx masked; do not claim Q35's shared IRQ11 without
     * proving PIRQ ownership. Existing lwIP worker polls this real NIC. */
    h10_io=(struct nas_e1000_io){
        .context=&h10_mmio,.mmio_bytes=H10_E1000_MMIO_BYTES,
        .read32=h10_read32,.write32=h10_write32
    };
    rc=nas_e1000_open(&h10_io,&nas_dma_qemu_coherent,&h10_session);
    if(rc)goto restore; /* Unsafe core failure keeps BAR/DMA pinned. */
    h10_persistent.core_live=true;
    for(unsigned i=0;i<NAS_NETDEV_MAC_BYTES;i++)
        h10_netdev.mac[i]=h10_session.mac[i];
    h10_netdev.name="e10000";
    h10_netdev.mtu=1500U;
    h10_netdev.backend=&h10_session;
    h10_netdev.ops=&h10_netdev_ops;
    h10_netdev.rx_irq_driven=false;
    rc=nas_netdev_register(&h10_netdev);
    if(rc) {
        if(nas_e1000_close(&h10_session))goto restore;
        h10_persistent.core_live=false;
        if(nas_pci_mmio_quiesced_unmap_release(
             &h10_owner,&h10_persistent.claim,H10_E1000_VIRT_BASE,true,
             nas_arch_x86_unmap_device_pages))goto restore;
        h10_pci_command_write(function->bus,function->slot,function->function,
                               resources->command);
        goto restore;
    }
    h10_persistent.attached=true;
    if(flags&(UINT64_C(1)<<9))__asm__ volatile("sti":::"memory");
    nas_print("[H10 E1000 NETDEV ATTACHED] name=e10000 MAC=");
    for(unsigned i=0;i<NAS_NETDEV_MAC_BYTES;i++){
        nas_hex(h10_session.mac[i]);
        if(i+1U<NAS_NETDEV_MAC_BYTES)nas_print(":");
    }
    nas_print(" 1500-MTU poll worker; no legacy INTx\n");
    return 0;
restore:
    if(flags&(UINT64_C(1)<<9))__asm__ volatile("sti":::"memory");
    return rc;
}

int nas_x86_h10_e1000_shutdown(void) {
    if(!h10_persistent.attached)return -ENODEV;
    if(h10_netdev.registered) {
        int rc=nas_netdev_begin_remove(&h10_netdev);
        if(rc)return rc;
        rc=nas_netdev_unregister(&h10_netdev);
        if(rc)return rc; /* Worker still owns a netif: retry deferred. */
    }
    if(h10_persistent.core_live) {
        int rc=nas_e1000_close(&h10_session);
        if(rc)return rc; /* Never unmap a DMA-active NIC. */
        h10_persistent.core_live=false;
    }
    int rc=nas_pci_mmio_quiesced_unmap_release(
        &h10_owner,&h10_persistent.claim,H10_E1000_VIRT_BASE,true,
        nas_arch_x86_unmap_device_pages);
    if(rc)return rc;
    h10_pci_command_write(h10_persistent.bus,h10_persistent.slot,
                           h10_persistent.function,
                           h10_persistent.original_command);
    h10_persistent.attached=false;
    nas_print("[H10 E1000 QUIESCED RELEASED] lwIP detached, TX/RX DMA halted, 32 BAR0 UC pages unmapped\n");
    return 0;
}
