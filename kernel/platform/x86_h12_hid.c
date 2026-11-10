#include "x86_h12_hid.h"
#include "x86_h11_bar.h"
#include "../arch/x86_64/mmu.h"
#include "../drivers/input/core.h"
#include "../drivers/usb/hid_boot.h"
#include "../drivers/usb/xhci.h"
#include "../drivers/serial.h"
#include "../errno.h"
#include <stdbool.h>
#include <stdint.h>

/* H12 USB Keyboard is an independent isolated QEMU q35 acceptance image.
 * H11's already witnessed high 64-bit BAR0 aperture and secure MMIO claim
 * rules are shared, but there is NO H11 USB storage or VirtIO Input device.
 * The H8 original signed Alpine root continues over NVMe. */
#define H12_XHCI_MMIO_BYTES UINT32_C(0x4000)
#define H12_XHCI_MMIO_PAGES (H12_XHCI_MMIO_BYTES/4096U)
#define H12_XHCI_VIRT_BASE UINT64_C(0xfffffe8001800000)

struct h12_mmio_mapping {uint64_t base;};
static struct h12_mmio_mapping h12_mmio={H12_XHCI_VIRT_BASE};
static struct nas_pci_mmio_owner h12_owner;
static struct nas_xhci_io h12_io;
static struct nas_xhci_ro_session h12_xhci;
static struct nas_usb_hid_boot_keyboard h12_report_state;
static struct nas_input_device h12_input;
static struct {
    bool attempted,attached,input_published;
    uint8_t bus,slot,function;
    uint16_t original_command;
    struct nas_pci_mmio_claim claim;
    uint64_t reports;
} h12_persistent;

static bool h12_is_qemu(void) {
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

static void h12_pci_write_command(uint8_t bus,uint8_t slot,uint8_t function,
                                  uint16_t command) {
    const uint32_t address=UINT32_C(0x80000000)|((uint32_t)bus<<16)|
        ((uint32_t)slot<<11)|((uint32_t)function<<8)|4U;
    const uint16_t select=0xcf8U,data=0xcfcU;
    __asm__ volatile("outl %0,%1"::"a"(address),"Nd"(select):"memory");
    /* Status at PCI config 0x06 is W1C; do not echo its live state. */
    __asm__ volatile("outl %0,%1"::"a"((uint32_t)command),"Nd"(data)
                     :"memory");
}

static int h12_read32(void *ctx,uint32_t offset,uint32_t *value) {
    const uint64_t base=ctx?((const struct h12_mmio_mapping*)ctx)->base:0;
    if(!base || !value || (offset&3U) ||
       offset>H12_XHCI_MMIO_BYTES-4U)return -EINVAL;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    *value=*(volatile const uint32_t *)(uintptr_t)(base+offset);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return 0;
}
static int h12_write32(void *ctx,uint32_t offset,uint32_t value) {
    const uint64_t base=ctx?((const struct h12_mmio_mapping*)ctx)->base:0;
    if(!base || (offset&3U) ||
       offset>H12_XHCI_MMIO_BYTES-4U)return -EINVAL;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    *(volatile uint32_t *)(uintptr_t)(base+offset)=value;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return 0;
}

static void h12_restore_if(uint64_t flags) {
    if(flags&(UINT64_C(1)<<9))__asm__ volatile("sti":::"memory");
}

int nas_x86_h12_hid_bind(const struct nas_pci_function_info *fn,
                         const struct nas_pci_resources *resources) {
    if(!fn || !resources)return -EINVAL;
    if(h12_persistent.attempted || fn->bus!=0U || fn->slot!=4U ||
       fn->function!=0U || fn->vendor_id!=0x1b36U ||
       fn->device_id!=0x000dU || fn->class_code!=0x0cU ||
       fn->subclass!=0x03U || fn->programming_interface!=0x30U ||
       !h12_is_qemu()) {
        nas_print("[H12 HID FAIL] unqualified QEMU PCI xHCI\n");
        return -ENODEV;
    }
    h12_persistent.attempted=true;
    const struct nas_pci_bar_desc *bar=NULL;
    for(unsigned i=0;i<resources->bar_count;i++)
        if(resources->bars[i].index==0U){bar=&resources->bars[i];break;}
    if(!nas_h11_qemu_xhci_bar_qualified(bar)) {
        nas_print("[H12 HID FAIL] unqualified MEM64 BAR0; 16MiB Q35 high whitelist only\n");
        return -ENODEV;
    }
    h12_owner=(struct nas_pci_mmio_owner){
        .window_base=NAS_H11_QEMU_MMIO_BASE,
        .window_bytes=NAS_H11_QEMU_MMIO_BYTES
    };
    struct nas_pci_mmio_claim claim={0};
    int rc=nas_pci_mmio_claim_bar(&h12_owner,resources,0U,
                                  H12_XHCI_MMIO_BYTES,&claim);
    if(rc)return rc;
    uint64_t flags=0;
    __asm__ volatile("pushfq; popq %0":"=r"(flags)::"memory");
    __asm__ volatile("cli":::"memory");
    rc=nas_pci_mmio_pin_mapping_attempt(&h12_owner,&claim);
    if(rc) {
        (void)nas_pci_mmio_release(&h12_owner,&claim);
        h12_restore_if(flags);
        return rc;
    }
    for(unsigned i=0;i<H12_XHCI_MMIO_PAGES;i++) {
        if(!nas_arch_x86_map_device_page(
            bar->base+(uint64_t)i*4096U,
            H12_XHCI_VIRT_BASE+(uint64_t)i*4096U)) {
            h12_restore_if(flags);
            return -EIO; /* Partial UC/NX mapping is pinned. */
        }
    }
    h12_persistent.claim=claim;
    h12_persistent.bus=fn->bus;
    h12_persistent.slot=fn->slot;
    h12_persistent.function=fn->function;
    h12_persistent.original_command=resources->command;
    nas_print("[H12 HID BAR QUALIFIED] QEMU 00:04.0 1b36:000d MEM64=");
    nas_hex(bar->base);
    nas_print(" 16KiB UC/NX\n");
    if((resources->command&UINT16_C(6))!=UINT16_C(6))
        h12_pci_write_command(fn->bus,fn->slot,fn->function,
                              (uint16_t)(resources->command|6U));
    h12_io=(struct nas_xhci_io){
        .context=&h12_mmio,.mmio_bytes=H12_XHCI_MMIO_BYTES,
        .read32=h12_read32,.write32=h12_write32
    };
    rc=nas_xhci_hid_open(&h12_io,&nas_dma_qemu_coherent,&h12_xhci);
    if(rc) {
        /* Do not release the BAR if a failed HCHalt still permits DMA.
         * Input publication never occurs on an incomplete USB open. */
        nas_print("[H12 HID FAIL DIAG] stage=");
        nas_dec((unsigned)h12_xhci.debug_stage);
        nas_print(" rc=");nas_dec((unsigned)(rc<0?-rc:rc));
        nas_print(" cc=");nas_dec((unsigned)h12_xhci.debug_completion_code);
        nas_print("\n");
        if(nas_xhci_hid_close(&h12_xhci)) {
            nas_print("[H12 HID PINNED] xHC halt unproven: DMA/BAR retained\n");
        } else if(!nas_pci_mmio_quiesced_unmap_release(
                       &h12_owner,&h12_persistent.claim,
                       H12_XHCI_VIRT_BASE,true,
                       nas_arch_x86_unmap_device_pages)) {
            h12_pci_write_command(fn->bus,fn->slot,fn->function,
                                   resources->command);
        }
        h12_restore_if(flags);
        nas_print("[H12 HID FAIL] required QEMU USB Boot Keyboard unavailable\n");
        return rc;
    }
    h12_persistent.attached=true;
    h12_restore_if(flags);
    nas_print("[H12 HID USB ENUMERATED] 8-byte Boot Keyboard interrupt-IN; input class publication deferred\n");
    return 0;
}

int nas_x86_h12_hid_publish_input(void) {
    if(!h12_persistent.attached || h12_persistent.input_published)
        return -ENODEV;
    nas_usb_hid_boot_init(&h12_report_state);
    nas_input_device_init(&h12_input,"QEMU USB HID Boot Keyboard");
    h12_input.id.bustype=3U; /* Linux BUS_USB */
    if(nas_input_set_capability(&h12_input,NAS_INPUT_EV_SYN,
                                NAS_INPUT_SYN_REPORT))return -EINVAL;
    unsigned key_caps=0;
    for(unsigned u=4U;u<=0x81U;u++) {
        uint16_t key=nas_usb_hid_boot_keycode((uint8_t)u);
        if(key && !nas_input_set_capability(&h12_input,
                                            NAS_INPUT_EV_KEY,key))key_caps++;
    }
    for(unsigned u=0xe0U;u<=0xe7U;u++) {
        uint16_t key=nas_usb_hid_boot_keycode((uint8_t)u);
        if(key && !nas_input_set_capability(&h12_input,
                                            NAS_INPUT_EV_KEY,key))key_caps++;
    }
    if(!key_caps || !(h12_input.keybit[30U/8U]&(1U<<(30U%8U))))
        return -EINVAL; /* Require actual KEY_A Linux evdev keyboard. */
    int rc=nas_input_register_device(&h12_input);
    if(rc)return rc;
    if(h12_input.index!=0U) {
        (void)nas_input_unregister_device(&h12_input);
        return -EEXIST; /* This fixture expects only one input device. */
    }
    h12_persistent.input_published=true;
    nas_print("[H12 HID INPUT ATTACHED] /dev/input/event0 QEMU USB Boot Keyboard EV_KEY/SYN_REPORT\n");
    return 0;
}

static void h12_emit(void *ctx,uint16_t type,uint16_t code,int32_t value) {
    struct nas_input_device *dev=ctx;
    nas_input_event(dev,type,code,value);
}

int nas_x86_h12_hid_poll(unsigned budget) {
    if(!h12_persistent.attached || !h12_persistent.input_published)
        return -ENODEV;
    if(!budget || budget>16U)return -EINVAL;
    unsigned reports=0;
    for(unsigned i=0;i<budget;i++) {
        uint8_t report[NAS_USB_HID_BOOT_REPORT_BYTES]={0};
        int rc=nas_xhci_hid_poll(&h12_xhci,report);
        if(rc==-EAGAIN)break;
        if(rc)return rc;
        ++reports;
        ++h12_persistent.reports;
        rc=nas_usb_hid_boot_report(&h12_report_state,report,
                                   sizeof(report),h12_emit,&h12_input);
        if(rc && rc!=-EAGAIN && rc!=-EPROTO)return rc;
        if(rc==-EPROTO)
            nas_print("[H12 HID REPORT REJECTED] malformed Boot Keyboard payload\n");
    }
    return (int)reports;
}

int nas_x86_h12_hid_shutdown(void) {
    if(!h12_persistent.attached)return -ENODEV;
    if(h12_persistent.input_published) {
        /* Drop logical pressed keys while evdev is still a live publisher.
         * PID1 already closed every input File before this orderly path. */
        nas_usb_hid_boot_release_all(&h12_report_state,h12_emit,&h12_input);
        int rc=nas_input_unregister_device(&h12_input);
        if(rc)return rc; /* A pinned reader forbids DMA/BAR teardown. */
        h12_persistent.input_published=false;
    }
    int rc=nas_xhci_hid_close(&h12_xhci);
    if(rc)return rc; /* Keep 7 DMA leases and BAR if xHC won't halt. */
    rc=nas_pci_mmio_quiesced_unmap_release(
        &h12_owner,&h12_persistent.claim,H12_XHCI_VIRT_BASE,true,
        nas_arch_x86_unmap_device_pages);
    if(rc)return rc;
    h12_pci_write_command(h12_persistent.bus,h12_persistent.slot,
                           h12_persistent.function,
                           h12_persistent.original_command);
    h12_persistent.attached=false;
    nas_print("[H12 HID QUIESCED RELEASED] event0 retired, xHC halted, seven DMA pages freed, 4 BAR0 UC pages unmapped\n");
    return 0;
}
