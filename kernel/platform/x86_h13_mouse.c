#include "x86_h13_mouse.h"
#include "x86_h11_bar.h"
#include "x86_qemu_hw_contract.h"
#include "../arch/x86_64/mmu.h"
#include "../drivers/input/core.h"
#include "../drivers/usb/hid_boot_mouse.h"
#include "../drivers/usb/xhci.h"
#include "../drivers/serial.h"
#include "../errno.h"
#include <stdbool.h>
#include <stdint.h>

/* H13 is QEMU-qualified only, independent of H11 storage and H12 keyboard.
 * The original signed Alpine evtest binary is loaded from immutable NVMe. */
#define H13_XHCI_VIRT_BASE UINT64_C(0xfffffe8001800000)

static struct nas_x86_qemu_mmio h13_mmio={H13_XHCI_VIRT_BASE,NAS_H11_XHCI_MMIO_BYTES};
static struct nas_pci_mmio_owner h13_owner;
static struct nas_xhci_io h13_io;
static struct nas_xhci_ro_session h13_xhci;
static struct nas_usb_hid_boot_mouse h13_report_state;
static struct nas_input_device h13_input;
static struct {
    bool attempted,attached,input_published;
    uint8_t bus,slot,function;
    uint16_t original_command;
    struct nas_pci_mmio_claim claim;
    uint64_t reports;
} h13_persistent;

int nas_x86_h13_mouse_bind(const struct nas_pci_function_info *fn,
                           const struct nas_pci_resources *resources) {
    if(!fn || !resources)return -EINVAL;
    if(h13_persistent.attempted || !nas_x86_qemu_xhci_function_qualified(fn)) {
        nas_print("[H13 MOUSE FAIL] unqualified QEMU PCI xHCI\n");
        return -ENODEV;
    }
    h13_persistent.attempted=true;
    const struct nas_pci_bar_desc *bar=nas_x86_qemu_find_bar(resources,0U);
    if(!nas_h11_qemu_xhci_bar_qualified(bar)) {
        nas_print("[H13 MOUSE FAIL] unqualified MEM64 BAR0; 16MiB Q35 high whitelist only\n");
        return -ENODEV;
    }
    struct nas_pci_mmio_claim claim={0};
    uint64_t flags=0;
    int rc=nas_x86_qemu_map_bar(&h13_owner,resources,bar,
                                H13_XHCI_VIRT_BASE,NAS_H11_XHCI_MMIO_BYTES,
                                NAS_H11_QEMU_MMIO_BASE,NAS_H11_QEMU_MMIO_BYTES,
                                &claim,&flags);
    if(rc)return rc;
    h13_persistent.claim=claim;
    h13_persistent.bus=fn->bus;
    h13_persistent.slot=fn->slot;
    h13_persistent.function=fn->function;
    h13_persistent.original_command=resources->command;
    nas_print("[H13 MOUSE BAR QUALIFIED] QEMU 00:04.0 1b36:000d MEM64=");
    nas_hex(bar->base);
    nas_print(" 16KiB UC/NX\n");
    if((resources->command&UINT16_C(6))!=UINT16_C(6))
        nas_x86_qemu_pci_command_write(fn->bus,fn->slot,fn->function,
                              (uint16_t)(resources->command|6U));
    h13_io=(struct nas_xhci_io){
        .context=&h13_mmio,.mmio_bytes=NAS_H11_XHCI_MMIO_BYTES,
        .read32=nas_x86_qemu_mmio_read32,.write32=nas_x86_qemu_mmio_write32
    };
    rc=nas_xhci_hid_mouse_open(&h13_io,&nas_dma_qemu_coherent,&h13_xhci);
    if(rc) {
        nas_print("[H13 MOUSE FAIL DIAG] stage=");
        nas_dec((unsigned)h13_xhci.debug_stage);
        nas_print(" rc=");nas_dec((unsigned)(rc<0?-rc:rc));
        nas_print(" cc=");nas_dec((unsigned)h13_xhci.debug_completion_code);
        nas_print("\n");
        if(nas_xhci_hid_mouse_close(&h13_xhci)) {
            nas_print("[H13 MOUSE PINNED] xHC halt unproven: DMA/BAR retained\n");
        } else if(!nas_pci_mmio_quiesced_unmap_release(
                       &h13_owner,&h13_persistent.claim,
                       H13_XHCI_VIRT_BASE,true,
                       nas_arch_x86_unmap_device_pages)) {
            nas_x86_qemu_pci_command_write(fn->bus,fn->slot,fn->function,
                                   resources->command);
        }
        nas_x86_qemu_restore_if(flags);
        nas_print("[H13 MOUSE FAIL] required QEMU USB Boot Mouse unavailable\n");
        return rc;
    }
    h13_persistent.attached=true;
    nas_x86_qemu_restore_if(flags);
    nas_print("[H13 MOUSE USB ENUMERATED] 4-byte Boot Mouse interrupt-IN; input class publication deferred\n");
    return 0;
}

int nas_x86_h13_mouse_publish_input(void) {
    if(!h13_persistent.attached || h13_persistent.input_published)
        return -ENODEV;
    nas_usb_hid_boot_mouse_init(&h13_report_state);
    nas_input_device_init(&h13_input,"QEMU USB HID Boot Mouse");
    h13_input.id.bustype=3U; /* Exact Linux BUS_USB for EVIOCGID. */
    if(nas_input_set_capability(&h13_input,NAS_INPUT_EV_SYN,
                                NAS_INPUT_SYN_REPORT) ||
       nas_input_set_capability(&h13_input,NAS_INPUT_EV_KEY,272U) ||
       nas_input_set_capability(&h13_input,NAS_INPUT_EV_KEY,273U) ||
       nas_input_set_capability(&h13_input,NAS_INPUT_EV_KEY,274U) ||
       nas_input_set_capability(&h13_input,NAS_INPUT_EV_REL,0U) ||
       nas_input_set_capability(&h13_input,NAS_INPUT_EV_REL,1U) ||
       nas_input_set_capability(&h13_input,NAS_INPUT_EV_REL,8U))
        return -EINVAL;
    int rc=nas_input_register_device(&h13_input);
    if(rc)return rc;
    if(h13_input.index!=0U) {
        (void)nas_input_unregister_device(&h13_input);
        return -EEXIST;
    }
    h13_persistent.input_published=true;
    nas_print("[H13 MOUSE INPUT ATTACHED] /dev/input/event0 QEMU USB HID Boot Mouse BUS_USB EV_REL/EV_KEY/SYN_REPORT\n");
    return 0;
}

static void h13_emit(void *ctx,uint16_t type,uint16_t code,int32_t value) {
    nas_input_event((struct nas_input_device *)ctx,type,code,value);
}

int nas_x86_h13_mouse_poll(unsigned budget) {
    if(!h13_persistent.attached || !h13_persistent.input_published)
        return -ENODEV;
    if(!budget || budget>16U)return -EINVAL;
    unsigned reports=0;
    for(unsigned i=0;i<budget;i++) {
        uint8_t report[NAS_USB_HID_BOOT_MOUSE_REPORT_BYTES]={0};
        int rc=nas_xhci_hid_mouse_poll(&h13_xhci,report);
        if(rc==-EAGAIN)break;
        if(rc)return rc;
        ++reports;
        ++h13_persistent.reports;
        rc=nas_usb_hid_boot_mouse_report(&h13_report_state,report,
                                         sizeof(report),h13_emit,&h13_input);
        if(rc && rc!=-EAGAIN && rc!=-EPROTO)return rc;
        if(rc==-EPROTO)
            nas_print("[H13 MOUSE REPORT REJECTED] malformed 4-byte Boot Mouse payload\n");
    }
    return (int)reports;
}

int nas_x86_h13_mouse_shutdown(void) {
    if(!h13_persistent.attached)return -ENODEV;
    if(h13_persistent.input_published) {
        nas_usb_hid_boot_mouse_release_all(&h13_report_state,h13_emit,
                                            &h13_input);
        int rc=nas_input_unregister_device(&h13_input);
        if(rc)return rc; /* Open evdev readers keep hardware DMA pinned. */
        h13_persistent.input_published=false;
    }
    int rc=nas_xhci_hid_mouse_close(&h13_xhci);
    if(rc)return rc;
    rc=nas_pci_mmio_quiesced_unmap_release(
        &h13_owner,&h13_persistent.claim,H13_XHCI_VIRT_BASE,true,
        nas_arch_x86_unmap_device_pages);
    if(rc)return rc;
    nas_x86_qemu_pci_command_write(h13_persistent.bus,h13_persistent.slot,
                           h13_persistent.function,
                           h13_persistent.original_command);
    h13_persistent.attached=false;
    nas_print("[H13 MOUSE QUIESCED RELEASED] event0 retired, xHC halted, seven DMA pages freed, 4 BAR0 UC pages unmapped\n");
    return 0;
}
