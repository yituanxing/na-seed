#include "x86_h10_e1000.h"
#include "../arch/x86_64/mmu.h"
#include "x86_qemu_hw_contract.h"
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
#define H10_E1000_VIRT_BASE UINT64_C(0xfffffe8001400000)
/* Bound to a fixed, isolated 256 MiB Q35/OVMF experiment. A firmware BAR
 * layout change must be investigated; never widen this from PCI BAR data. */
#define H10_QEMU_PCI_MMIO_BASE UINT64_C(0x81000000)
#define H10_QEMU_PCI_MMIO_BYTES UINT64_C(0x01000000)
#define H10_QEMU_PCI_MMIO_END (H10_QEMU_PCI_MMIO_BASE+H10_QEMU_PCI_MMIO_BYTES)

static struct nas_x86_qemu_mmio h10_mmio={H10_E1000_VIRT_BASE,H10_E1000_MMIO_BYTES};
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
       !nas_x86_qemu_fw_cfg_signature()) {
        nas_print("[H10 E1000 FAIL] unqualified QEMU 82540EM function\n");
        return -ENODEV;
    }
    h10_persistent.attempted=true;
    const struct nas_pci_bar_desc *bar=nas_x86_qemu_find_bar(resources,0U);
    if(!bar || bar->kind!=NAS_PCI_BAR_MEM32 || bar->prefetchable ||
       (bar->base&4095U) || bar->base<H10_QEMU_PCI_MMIO_BASE ||
       bar->base>H10_QEMU_PCI_MMIO_END-H10_E1000_MMIO_BYTES) {
        nas_print("[H10 E1000 FAIL] unqualified QEMU BAR0\n");
        return -ENODEV;
    }
    struct nas_pci_mmio_claim claim={0};
    uint64_t flags=0;
    int rc=nas_x86_qemu_map_bar(&h10_owner,resources,bar,H10_E1000_VIRT_BASE,
                                H10_E1000_MMIO_BYTES,H10_QEMU_PCI_MMIO_BASE,
                                H10_QEMU_PCI_MMIO_BYTES,&claim,&flags);
    if(rc)return rc;
    h10_persistent.claim=claim;
    h10_persistent.bus=function->bus;
    h10_persistent.slot=function->slot;
    h10_persistent.function=function->function;
    h10_persistent.original_command=resources->command;
    nas_print("[H10 E1000 BAR QUALIFIED] 00:04.0 BAR0=");
    nas_hex(bar->base);
    nas_print(" 128KiB UC/NX QEMU-only\n");
    if((resources->command&UINT16_C(6))!=UINT16_C(6))
        nas_x86_qemu_pci_command_write(function->bus,function->slot,
                               function->function,
                               (uint16_t)(resources->command|6U));
    /* H10 first end-to-end network gate is bounded NAPI-style polling.
     * Leave legacy INTx masked; do not claim Q35's shared IRQ11 without
     * proving PIRQ ownership. Existing lwIP worker polls this real NIC. */
    h10_io=(struct nas_e1000_io){
        .context=&h10_mmio,.mmio_bytes=H10_E1000_MMIO_BYTES,
        .read32=nas_x86_qemu_mmio_read32,.write32=nas_x86_qemu_mmio_write32
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
        nas_x86_qemu_pci_command_write(function->bus,function->slot,function->function,
                               resources->command);
        goto restore;
    }
    h10_persistent.attached=true;
    nas_x86_qemu_restore_if(flags);
    nas_print("[H10 E1000 NETDEV ATTACHED] name=e10000 MAC=");
    for(unsigned i=0;i<NAS_NETDEV_MAC_BYTES;i++){
        nas_hex(h10_session.mac[i]);
        if(i+1U<NAS_NETDEV_MAC_BYTES)nas_print(":");
    }
    nas_print(" 1500-MTU poll worker; no legacy INTx\n");
    return 0;
restore:
    nas_x86_qemu_restore_if(flags);
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
    nas_x86_qemu_pci_command_write(h10_persistent.bus,h10_persistent.slot,
                           h10_persistent.function,
                           h10_persistent.original_command);
    h10_persistent.attached=false;
    nas_print("[H10 E1000 QUIESCED RELEASED] lwIP detached, TX/RX DMA halted, 32 BAR0 UC pages unmapped\n");
    return 0;
}
