#include "x86_h14_dual_hid.h"
#include "x86_h11_bar.h"
#include "../arch/x86_64/mmu.h"
#include "../drivers/input/core.h"
#include "../drivers/usb/hid_boot_mouse.h"
#include "../drivers/usb/hid_boot.h"
#include "../drivers/usb/xhci.h"
#include "../drivers/serial.h"
#include "../errno.h"
#include <stdbool.h>
#include <stdint.h>

/* H14 is QEMU-qualified: one xHC owns two separate keyboard/mouse root ports.
 * Only the immutable NVMe Alpine root supplies the original signed evtest. */
#define H14_XHCI_MMIO_BYTES UINT32_C(0x4000)
#define H14_XHCI_MMIO_PAGES (H14_XHCI_MMIO_BYTES/4096U)
#define H14_XHCI_VIRT_BASE UINT64_C(0xfffffe8001800000)

struct h14_mmio_mapping {uint64_t base;};
static struct h14_mmio_mapping h14_mmio={H14_XHCI_VIRT_BASE};
static struct nas_pci_mmio_owner h14_owner;
static struct nas_xhci_io h14_io;
static struct nas_xhci_ro_session h14_xhci;
static struct nas_usb_hid_boot_mouse h14_mouse_state;
static struct nas_usb_hid_boot_keyboard h14_keyboard_state;
static struct nas_input_device h14_mouse_input;
static struct nas_input_device h14_keyboard_input;
static struct {
    bool attempted,attached,input_published;
    bool keyboard_published,mouse_published;
    uint8_t bus,slot,function;
    uint16_t original_command;
    struct nas_pci_mmio_claim claim;
    uint64_t reports;
} h14_persistent;

static bool h14_is_qemu(void) {
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

static void h14_pci_write_command(uint8_t bus,uint8_t slot,uint8_t function,
                                  uint16_t command) {
    const uint32_t address=UINT32_C(0x80000000)|((uint32_t)bus<<16)|
        ((uint32_t)slot<<11)|((uint32_t)function<<8)|4U;
    const uint16_t select=0xcf8U,data=0xcfcU;
    __asm__ volatile("outl %0,%1"::"a"(address),"Nd"(select):"memory");
    /* PCI status is W1C: only restore the saved command halfword. */
    __asm__ volatile("outl %0,%1"::"a"((uint32_t)command),"Nd"(data)
                     :"memory");
}

static int h14_read32(void *ctx,uint32_t offset,uint32_t *value) {
    const uint64_t base=ctx?((const struct h14_mmio_mapping*)ctx)->base:0;
    if(!base || !value || (offset&3U) ||
       offset>H14_XHCI_MMIO_BYTES-4U)return -EINVAL;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    *value=*(volatile const uint32_t *)(uintptr_t)(base+offset);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return 0;
}

static int h14_write32(void *ctx,uint32_t offset,uint32_t value) {
    const uint64_t base=ctx?((const struct h14_mmio_mapping*)ctx)->base:0;
    if(!base || (offset&3U) ||
       offset>H14_XHCI_MMIO_BYTES-4U)return -EINVAL;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    *(volatile uint32_t *)(uintptr_t)(base+offset)=value;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return 0;
}

static void h14_restore_if(uint64_t flags) {
    if(flags&(UINT64_C(1)<<9))__asm__ volatile("sti":::"memory");
}

int nas_x86_h14_dual_hid_bind(const struct nas_pci_function_info *fn,
                           const struct nas_pci_resources *resources) {
    if(!fn || !resources)return -EINVAL;
    if(h14_persistent.attempted || fn->bus!=0U || fn->slot!=4U ||
       fn->function!=0U || fn->vendor_id!=0x1b36U ||
       fn->device_id!=0x000dU || fn->class_code!=0x0cU ||
       fn->subclass!=0x03U || fn->programming_interface!=0x30U ||
       !h14_is_qemu()) {
        nas_print("[H14 DUAL FAIL] unqualified QEMU PCI xHCI\n");
        return -ENODEV;
    }
    h14_persistent.attempted=true;
    const struct nas_pci_bar_desc *bar=NULL;
    for(unsigned i=0;i<resources->bar_count;i++)
        if(resources->bars[i].index==0U){bar=&resources->bars[i];break;}
    if(!nas_h11_qemu_xhci_bar_qualified(bar)) {
        nas_print("[H14 DUAL FAIL] unqualified MEM64 BAR0; 16MiB Q35 high whitelist only\n");
        return -ENODEV;
    }
    h14_owner=(struct nas_pci_mmio_owner){
        .window_base=NAS_H11_QEMU_MMIO_BASE,
        .window_bytes=NAS_H11_QEMU_MMIO_BYTES
    };
    struct nas_pci_mmio_claim claim={0};
    int rc=nas_pci_mmio_claim_bar(&h14_owner,resources,0U,
                                  H14_XHCI_MMIO_BYTES,&claim);
    if(rc)return rc;
    uint64_t flags=0;
    __asm__ volatile("pushfq; popq %0":"=r"(flags)::"memory");
    __asm__ volatile("cli":::"memory");
    rc=nas_pci_mmio_pin_mapping_attempt(&h14_owner,&claim);
    if(rc) {
        (void)nas_pci_mmio_release(&h14_owner,&claim);
        h14_restore_if(flags);
        return rc;
    }
    for(unsigned i=0;i<H14_XHCI_MMIO_PAGES;i++) {
        if(!nas_arch_x86_map_device_page(
            bar->base+(uint64_t)i*4096U,
            H14_XHCI_VIRT_BASE+(uint64_t)i*4096U)) {
            h14_restore_if(flags);
            return -EIO; /* Partial UC/NX mapping must remain pinned. */
        }
    }
    h14_persistent.claim=claim;
    h14_persistent.bus=fn->bus;
    h14_persistent.slot=fn->slot;
    h14_persistent.function=fn->function;
    h14_persistent.original_command=resources->command;
    nas_print("[H14 DUAL BAR QUALIFIED] QEMU 00:04.0 1b36:000d MEM64=");
    nas_hex(bar->base);
    nas_print(" 16KiB UC/NX\n");
    if((resources->command&UINT16_C(6))!=UINT16_C(6))
        h14_pci_write_command(fn->bus,fn->slot,fn->function,
                              (uint16_t)(resources->command|6U));
    h14_io=(struct nas_xhci_io){
        .context=&h14_mmio,.mmio_bytes=H14_XHCI_MMIO_BYTES,
        .read32=h14_read32,.write32=h14_write32
    };
    rc=nas_xhci_dual_hid_open(&h14_io,&nas_dma_qemu_coherent,&h14_xhci);
    if(rc) {
        nas_print("[H14 DUAL FAIL DIAG] stage=");
        nas_dec((unsigned)h14_xhci.debug_stage);
        nas_print(" rc=");nas_dec((unsigned)(rc<0?-rc:rc));
        nas_print(" cc=");nas_dec((unsigned)h14_xhci.debug_completion_code);
        nas_print("\n");
        const uint8_t missing=h14_xhci.dual_missing_protocol;
        if(nas_xhci_dual_hid_close(&h14_xhci)) {
            nas_print("[H14 DUAL PINNED] xHC halt unproven: DMA/BAR retained\n");
        } else if(!nas_pci_mmio_quiesced_unmap_release(
                       &h14_owner,&h14_persistent.claim,
                       H14_XHCI_VIRT_BASE,true,
                       nas_arch_x86_unmap_device_pages)) {
            h14_pci_write_command(fn->bus,fn->slot,fn->function,
                                   resources->command);
        }
        h14_restore_if(flags);
        if(missing==1U)
            nas_print("[H14 DUAL FAIL] required USB Boot Keyboard unavailable\n");
        else if(missing==2U)
            nas_print("[H14 DUAL FAIL] required USB Boot Mouse unavailable\n");
        else
            nas_print("[H14 DUAL FAIL] shared xHCI controller initialization failed\n");
        return rc;
    }
    h14_persistent.attached=true;
    h14_restore_if(flags);
    nas_print("[H14 DUAL USB ENUMERATED] same QEMU PCI xHCI, two physical root ports; 8B Boot Keyboard and 4B Boot Mouse Interrupt-IN\n");
    return 0;
}


int nas_x86_h14_dual_hid_publish_input(void) {
    if(!h14_persistent.attached || h14_persistent.input_published ||
       h14_persistent.keyboard_published || h14_persistent.mouse_published)
        return -ENODEV;
    nas_usb_hid_boot_init(&h14_keyboard_state);
    nas_usb_hid_boot_mouse_init(&h14_mouse_state);
    nas_input_device_init(&h14_keyboard_input,"QEMU USB HID Boot Keyboard");
    h14_keyboard_input.id.bustype=3U; /* Linux BUS_USB */
    if(nas_input_set_capability(&h14_keyboard_input,NAS_INPUT_EV_SYN,
                                NAS_INPUT_SYN_REPORT))return -EINVAL;
    unsigned key_caps=0;
    for(unsigned u=4U;u<=0x81U;u++) {
        uint16_t key=nas_usb_hid_boot_keycode((uint8_t)u);
        if(key && !nas_input_set_capability(&h14_keyboard_input,
                                            NAS_INPUT_EV_KEY,key))key_caps++;
    }
    for(unsigned u=0xe0U;u<=0xe7U;u++) {
        uint16_t key=nas_usb_hid_boot_keycode((uint8_t)u);
        if(key && !nas_input_set_capability(&h14_keyboard_input,
                                            NAS_INPUT_EV_KEY,key))key_caps++;
    }
    if(!key_caps ||
       !(h14_keyboard_input.keybit[30U/8U]&(1U<<(30U%8U))))
        return -EINVAL;
    int rc=nas_input_register_device(&h14_keyboard_input);
    if(rc)return rc;
    if(h14_keyboard_input.index!=0U) {
        (void)nas_input_unregister_device(&h14_keyboard_input);
        return -EEXIST;
    }
    h14_persistent.keyboard_published=true;

    nas_input_device_init(&h14_mouse_input,"QEMU USB HID Boot Mouse");
    h14_mouse_input.id.bustype=3U;
    if(nas_input_set_capability(&h14_mouse_input,NAS_INPUT_EV_SYN,
                                NAS_INPUT_SYN_REPORT) ||
       nas_input_set_capability(&h14_mouse_input,NAS_INPUT_EV_KEY,272U) ||
       nas_input_set_capability(&h14_mouse_input,NAS_INPUT_EV_KEY,273U) ||
       nas_input_set_capability(&h14_mouse_input,NAS_INPUT_EV_KEY,274U) ||
       nas_input_set_capability(&h14_mouse_input,NAS_INPUT_EV_REL,0U) ||
       nas_input_set_capability(&h14_mouse_input,NAS_INPUT_EV_REL,1U) ||
       nas_input_set_capability(&h14_mouse_input,NAS_INPUT_EV_REL,8U))
        goto rollback_keyboard;
    rc=nas_input_register_device(&h14_mouse_input);
    if(rc)goto rollback_keyboard;
    if(h14_mouse_input.index!=1U) {
        (void)nas_input_unregister_device(&h14_mouse_input);
        goto rollback_keyboard;
    }
    h14_persistent.mouse_published=true;
    h14_persistent.input_published=true;
    nas_print("[H14 DUAL KEYBOARD ATTACHED] /dev/input/event0 QEMU USB HID Boot Keyboard BUS_USB\n");
    nas_print("[H14 DUAL MOUSE ATTACHED] /dev/input/event1 QEMU USB HID Boot Mouse BUS_USB\n");
    return 0;
rollback_keyboard:
    if(nas_input_unregister_device(&h14_keyboard_input))
        return -EBUSY; /* Fail closed; do not permit xHC DMA teardown. */
    h14_persistent.keyboard_published=false;
    return -EINVAL;
}

static void h14_emit(void *ctx,uint16_t type,uint16_t code,int32_t value) {
    nas_input_event((struct nas_input_device *)ctx,type,code,value);
}

int nas_x86_h14_dual_hid_poll(unsigned budget) {
    if(!h14_persistent.attached || !h14_persistent.input_published)
        return -ENODEV;
    if(!budget || budget>16U)return -EINVAL;
    unsigned reports=0;
    for(unsigned i=0;i<budget;i++) {
        uint8_t report[NAS_USB_HID_BOOT_REPORT_BYTES]={0};
        int rc=nas_xhci_dual_hid_poll_keyboard(&h14_xhci,report);
        if(rc==-EAGAIN)break;
        if(rc)return rc;
        ++reports;
        ++h14_persistent.reports;
        rc=nas_usb_hid_boot_report(&h14_keyboard_state,report,
                                   sizeof(report),h14_emit,&h14_keyboard_input);
        if(rc && rc!=-EAGAIN && rc!=-EPROTO)return rc;
        if(rc==-EPROTO)
            nas_print("[H14 DUAL KBD REPORT REJECTED] malformed report\n");
    }
    for(unsigned i=0;i<budget;i++) {
        uint8_t report[NAS_USB_HID_BOOT_MOUSE_REPORT_BYTES]={0};
        int rc=nas_xhci_dual_hid_poll_mouse(&h14_xhci,report);
        if(rc==-EAGAIN)break;
        if(rc)return rc;
        ++reports;
        ++h14_persistent.reports;
        rc=nas_usb_hid_boot_mouse_report(&h14_mouse_state,report,
                                         sizeof(report),h14_emit,&h14_mouse_input);
        if(rc && rc!=-EAGAIN && rc!=-EPROTO)return rc;
        if(rc==-EPROTO)
            nas_print("[H14 DUAL MOUSE REPORT REJECTED] malformed report\n");
    }
    return (int)reports;
}

int nas_x86_h14_dual_hid_shutdown(void) {
    if(!h14_persistent.attached)return -ENODEV;
    if(h14_persistent.mouse_published) {
        nas_usb_hid_boot_mouse_release_all(&h14_mouse_state,h14_emit,
                                            &h14_mouse_input);
        int rc=nas_input_unregister_device(&h14_mouse_input);
        if(rc)return rc; /* Live evdev readers pin DMA/BAR. */
        h14_persistent.mouse_published=false;
    }
    if(h14_persistent.keyboard_published) {
        nas_usb_hid_boot_release_all(&h14_keyboard_state,h14_emit,
                                      &h14_keyboard_input);
        int rc=nas_input_unregister_device(&h14_keyboard_input);
        if(rc)return rc;
        h14_persistent.keyboard_published=false;
    }
    h14_persistent.input_published=false;
    int rc=nas_xhci_dual_hid_close(&h14_xhci);
    if(rc)return rc; /* Unsafe DMA stop: keep BAR pinned. */
    rc=nas_pci_mmio_quiesced_unmap_release(
        &h14_owner,&h14_persistent.claim,H14_XHCI_VIRT_BASE,true,
        nas_arch_x86_unmap_device_pages);
    if(rc)return rc;
    h14_pci_write_command(h14_persistent.bus,h14_persistent.slot,
                           h14_persistent.function,
                           h14_persistent.original_command);
    h14_persistent.attached=false;
    nas_print("[H14 DUAL QUIESCED RELEASED] event0/event1 retired, xHC halted, DMA leases freed, 4 BAR0 UC pages unmapped\n");
    return 0;
}
