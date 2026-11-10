#include "x86_h15_ftdi.h"
#include "x86_h11_bar.h"
#include "../arch/x86_64/mmu.h"
#include "../drivers/usb/xhci.h"
#include "../drivers/serial.h"
#include "../drivers/port/dma.h"
#include "../fs/usb_serial_chrdev.h"
#include "../errno.h"
#include <stdbool.h>
#include <stdint.h>

/* Same narrowly qualified Q35 xHCI resource lifetime as H12-H14.
 * One 0403:6001 FT232BM is configured by the xHCI FTDI transport to 8N1,
 * 115200 baud; no synthetic or UART-console RX may enter this queue. */
#define H15_XHCI_MMIO_BYTES UINT32_C(0x4000)
#define H15_XHCI_MMIO_PAGES (H15_XHCI_MMIO_BYTES / 4096U)
#define H15_XHCI_VIRT_BASE UINT64_C(0xfffffe8001800000)
#define H15_RX_CAP 1024U

struct h15_mmio_mapping { uint64_t base; };
static struct h15_mmio_mapping h15_mmio={ H15_XHCI_VIRT_BASE };
static struct nas_pci_mmio_owner h15_owner;
static struct nas_xhci_io h15_io;
static struct nas_xhci_ro_session h15_xhci;
static struct {
    bool attempted,attached;
    uint8_t bus,slot,function;
    uint16_t original_command;
    struct nas_pci_mmio_claim claim;
    uint8_t rx[H15_RX_CAP];
    unsigned rx_head,rx_count;
    uint64_t bulk_in_bytes,bulk_out_bytes;
} h15;

static bool h15_is_qemu(void) {
    static const char expected[4]={'Q','E','M','U'};
    const uint16_t select=0x510U,data=0x511U;
    __asm__ volatile("outw %0,%1" :: "a"((uint16_t)0),"Nd"(select)
                     :"memory");
    for(unsigned i=0;i<sizeof(expected);i++) {
        uint8_t value=0;
        __asm__ volatile("inb %1,%0":"=a"(value):"Nd"(data):"memory");
        if(value!=(uint8_t)expected[i])return false;
    }
    return true;
}

static void h15_pci_write_command(uint8_t bus,uint8_t slot,
                                  uint8_t function,uint16_t command) {
    const uint32_t address=UINT32_C(0x80000000)|((uint32_t)bus<<16)|
        ((uint32_t)slot<<11)|((uint32_t)function<<8)|4U;
    const uint16_t select=0xcf8U,data=0xcfcU;
    __asm__ volatile("outl %0,%1"::"a"(address),"Nd"(select):"memory");
    /* PCI Status is W1C: restore only the saved Command halfword. */
    __asm__ volatile("outl %0,%1"::"a"((uint32_t)command),"Nd"(data)
                     :"memory");
}
static int h15_read32(void *ctx,uint32_t offset,uint32_t *value) {
    const uint64_t base=ctx?((const struct h15_mmio_mapping*)ctx)->base:0;
    if(!base || !value || (offset&3U) ||
       offset>H15_XHCI_MMIO_BYTES-4U)return -EINVAL;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    *value=*(volatile const uint32_t *)(uintptr_t)(base+offset);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return 0;
}
static int h15_write32(void *ctx,uint32_t offset,uint32_t value) {
    const uint64_t base=ctx?((const struct h15_mmio_mapping*)ctx)->base:0;
    if(!base || (offset&3U) ||
       offset>H15_XHCI_MMIO_BYTES-4U)return -EINVAL;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    *(volatile uint32_t *)(uintptr_t)(base+offset)=value;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return 0;
}
static void h15_restore_if(uint64_t flags) {
    if(flags&(UINT64_C(1)<<9))__asm__ volatile("sti":::"memory");
}
static int h15_release_qualified_bar(void) {
    int rc=nas_pci_mmio_quiesced_unmap_release(
        &h15_owner,&h15.claim,H15_XHCI_VIRT_BASE,true,
        nas_arch_x86_unmap_device_pages);
    if(!rc)h15_pci_write_command(h15.bus,h15.slot,h15.function,
                                h15.original_command);
    return rc;
}

int nas_x86_h15_ftdi_bind(const struct nas_pci_function_info *fn,
                          const struct nas_pci_resources *resources) {
    if(!fn || !resources)return -EINVAL;
    if(h15.attempted || fn->bus!=0U || fn->slot!=4U ||
       fn->function!=0U || fn->vendor_id!=0x1b36U ||
       fn->device_id!=0x000dU || fn->class_code!=0x0cU ||
       fn->subclass!=0x03U || fn->programming_interface!=0x30U ||
       !h15_is_qemu()) {
        nas_print("[H15 FTDI FAIL] unqualified QEMU PCI xHCI\n");
        return -ENODEV;
    }
    h15.attempted=true;
    const struct nas_pci_bar_desc *bar=NULL;
    for(unsigned i=0;i<resources->bar_count;i++)
        if(resources->bars[i].index==0U){bar=&resources->bars[i];break;}
    if(!nas_h11_qemu_xhci_bar_qualified(bar)) {
        nas_print("[H15 FTDI FAIL] xHCI MEM64 BAR0 outside Q35 whitelist\n");
        return -ENODEV;
    }
    h15_owner=(struct nas_pci_mmio_owner){
        .window_base=NAS_H11_QEMU_MMIO_BASE,
        .window_bytes=NAS_H11_QEMU_MMIO_BYTES
    };
    struct nas_pci_mmio_claim claim={0};
    int rc=nas_pci_mmio_claim_bar(&h15_owner,resources,0U,
                                 H15_XHCI_MMIO_BYTES,&claim);
    if(rc)return rc;
    uint64_t flags=0;
    __asm__ volatile("pushfq; popq %0":"=r"(flags)::"memory");
    __asm__ volatile("cli":::"memory");
    rc=nas_pci_mmio_pin_mapping_attempt(&h15_owner,&claim);
    if(rc) {
        (void)nas_pci_mmio_release(&h15_owner,&claim);
        h15_restore_if(flags);
        return rc;
    }
    for(unsigned i=0;i<H15_XHCI_MMIO_PAGES;i++) {
        if(!nas_arch_x86_map_device_page(
            bar->base+(uint64_t)i*4096U,
            H15_XHCI_VIRT_BASE+(uint64_t)i*4096U)) {
            h15_restore_if(flags);
            return -EIO; /* Unproven partial mapping remains pinned. */
        }
    }
    h15.claim=claim;
    h15.bus=fn->bus;
    h15.slot=fn->slot;
    h15.function=fn->function;
    h15.original_command=resources->command;
    nas_print("[H15 FTDI BAR QUALIFIED] QEMU 00:04.0 1b36:000d MEM64=");
    nas_hex(bar->base);
    nas_print(" 16KiB UC/NX\n");
    if((resources->command&UINT16_C(6))!=UINT16_C(6))
        h15_pci_write_command(fn->bus,fn->slot,fn->function,
                               (uint16_t)(resources->command|6U));
    h15_io=(struct nas_xhci_io){
        .context=&h15_mmio,.mmio_bytes=H15_XHCI_MMIO_BYTES,
        .read32=h15_read32,.write32=h15_write32
    };
    rc=nas_xhci_ftdi_open(&h15_io,&nas_dma_qemu_coherent,&h15_xhci);
    if(rc) {
        nas_print("[H15 FTDI FAIL DIAG] stage=");
        nas_dec((unsigned)h15_xhci.debug_stage);
        nas_print(" rc=");nas_dec((unsigned)(rc<0?-rc:rc));
        nas_print(" cc=");nas_dec((unsigned)h15_xhci.debug_completion_code);
        nas_print("\n");
        if(nas_xhci_ftdi_close(&h15_xhci)) {
            nas_print("[H15 FTDI PINNED] xHC halt unproven, keep DMA/BAR\n");
        } else if(h15_release_qualified_bar()) {
            nas_print("[H15 FTDI PINNED] BAR unmap/release failed\n");
        }
        h15_restore_if(flags);
        nas_print("[H15 FTDI FAIL] required QEMU FT232BM 0403:6001 unavailable; no original Alpine userland\n");
        return rc;
    }
    h15.attached=true;
    h15_restore_if(flags);
    nas_print("[H15 FTDI USB ENUMERATED] QEMU xHCI root port; genuine FT232BM 0403:6001 vendor-control 8N1 115200; Bulk-IN/OUT\n");
    return 0;
}
bool nas_x86_h15_ftdi_attached(void) { return h15.attached; }
unsigned nas_x86_h15_ftdi_rx_count(void) { return h15.attached?h15.rx_count:0U; }
void nas_x86_h15_ftdi_rx_flush(void) { h15.rx_head=h15.rx_count=0U; }

int nas_x86_h15_ftdi_poll(unsigned budget) {
    if(!h15.attached)return -ENODEV;
    if(!budget || budget>16U)return -EINVAL;
    unsigned received_total=0;
    for(unsigned i=0;i<budget;i++) {
        /* One real FTDI Bulk-IN packet can carry up to 62 data bytes after
         * its two modem-status bytes. Preserve backpressure, never invent. */
        if(H15_RX_CAP-h15.rx_count<62U)break;
        uint8_t data[62];
        uint32_t received=0;
        int rc=nas_xhci_ftdi_poll_in(&h15_xhci,data,sizeof(data),&received);
        if(rc==-EAGAIN)break;
        if(rc)return rc;
        if(!received || received>sizeof(data))return -EPROTO;
        for(uint32_t j=0;j<received;j++) {
            h15.rx[(h15.rx_head+h15.rx_count)%H15_RX_CAP]=data[j];
            h15.rx_count++;
        }
        received_total+=received;
        h15.bulk_in_bytes+=received;
    }
    return (int)received_total;
}
int nas_x86_h15_ftdi_read(uint8_t *dst,uint32_t bytes) {
    if(!h15.attached)return -ENODEV;
    if(bytes && !dst)return -EINVAL;
    if(!bytes)return 0;
    if(!h15.rx_count)return -EAGAIN;
    unsigned take=bytes;
    if(take>h15.rx_count)take=h15.rx_count;
    for(unsigned i=0;i<take;i++)
        dst[i]=h15.rx[(h15.rx_head+i)%H15_RX_CAP];
    h15.rx_head=(h15.rx_head+take)%H15_RX_CAP;
    h15.rx_count-=take;
    return (int)take;
}
int nas_x86_h15_ftdi_write(const uint8_t *src,uint32_t bytes) {
    if(!h15.attached)return -ENODEV;
    if(bytes && !src)return -EINVAL;
    uint32_t done=0;
    while(done<bytes) {
        uint32_t chunk=bytes-done;
        if(chunk>64U)chunk=64U;
        int rc=nas_xhci_ftdi_write(&h15_xhci,src+done,chunk);
        if(rc)return done?(int)done:rc;
        done+=chunk;
        h15.bulk_out_bytes+=chunk;
    }
    return (int)done;
}
int nas_x86_h15_ftdi_shutdown(void) {
    if(!h15.attached)return -ENODEV;
    /* The class unpublish refuses live File pins before any DMA frees. */
    int rc=nas_usb_serial_chrdev_unpublish();
    if(rc)return rc;
    rc=nas_xhci_ftdi_close(&h15_xhci);
    if(rc)return rc; /* Unsafe stop: keep MMIO BAR pinned. */
    rc=h15_release_qualified_bar();
    if(rc)return rc;
    h15.attached=false;
    nas_print("[H15 FTDI QUIESCED RELEASED] ttyUSB0 retired, xHC HCHalted, 7 DMA pages freed, 4 BAR0 UC pages unmapped; IN=");
    nas_dec((unsigned)h15.bulk_in_bytes);
    nas_print(" OUT=");
    nas_dec((unsigned)h15.bulk_out_bytes);
    nas_print("\n");
    return 0;
}
