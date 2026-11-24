#include "x86_h13_mouse.h"
#include "x86_qemu_hid_input.h"
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

static struct nas_x86_qemu_xhci h13_hw={.mmio={H13_XHCI_VIRT_BASE,NAS_H11_XHCI_MMIO_BYTES}};
static struct nas_xhci_ro_session h13_xhci;
static struct nas_usb_hid_boot_mouse h13_report_state;
static struct nas_input_device h13_input;
static struct {
    bool attached,input_published;
    uint64_t reports;
} h13_persistent;

int nas_x86_h13_mouse_bind(const struct nas_pci_function_info *fn,
                           const struct nas_pci_resources *resources) {
    uint64_t flags=0;
    int rc=nas_x86_qemu_xhci_prepare(&h13_hw,fn,resources,&flags,
        "[H13 MOUSE FAIL] unqualified QEMU PCI xHCI\n",
        "[H13 MOUSE FAIL] unqualified MEM64 BAR0; 16MiB Q35 high whitelist only\n",
        "[H13 MOUSE BAR QUALIFIED] QEMU 00:04.0 1b36:000d MEM64="," 16KiB UC/NX\n");
    if(rc)return rc;
    rc=nas_xhci_hid_mouse_open(&h13_hw.io,&nas_dma_qemu_coherent,&h13_xhci);
    if(rc) {
        nas_print("[H13 MOUSE FAIL DIAG] stage=");
        nas_dec((unsigned)h13_xhci.debug_stage);
        nas_print(" rc=");nas_dec((unsigned)(rc<0?-rc:rc));
        nas_print(" cc=");nas_dec((unsigned)h13_xhci.debug_completion_code);
        nas_print("\n");
        if(nas_xhci_hid_mouse_close(&h13_xhci)) {
            nas_print("[H13 MOUSE PINNED] xHC halt unproven: DMA/BAR retained\n");
        } else {
            (void)nas_x86_qemu_xhci_release(&h13_hw);
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
    int rc=nas_x86_qemu_hid_mouse_register(&h13_input,0U);
    if(rc)return rc;
    h13_persistent.input_published=true;
    nas_print("[H13 MOUSE INPUT ATTACHED] /dev/input/event0 QEMU USB HID Boot Mouse BUS_USB EV_REL/EV_KEY/SYN_REPORT\n");
    return 0;
}

int nas_x86_h13_mouse_poll(unsigned budget) {
    if(!h13_persistent.attached || !h13_persistent.input_published)
        return -ENODEV;
    if(!budget || budget>16U)return -EINVAL;
    return nas_x86_qemu_hid_mouse_poll(&h13_xhci,nas_xhci_hid_mouse_poll,
        &h13_report_state,&h13_input,budget,&h13_persistent.reports,"[H13 MOUSE REPORT REJECTED] malformed 4-byte Boot Mouse payload\n");
}

int nas_x86_h13_mouse_shutdown(void) {
    if(!h13_persistent.attached)return -ENODEV;
    if(h13_persistent.input_published) {
        nas_usb_hid_boot_mouse_release_all(&h13_report_state,nas_x86_qemu_hid_emit,
                                            &h13_input);
        int rc=nas_input_unregister_device(&h13_input);
        if(rc)return rc; /* Open evdev readers keep hardware DMA pinned. */
        h13_persistent.input_published=false;
    }
    int rc=nas_xhci_hid_mouse_close(&h13_xhci);
    if(rc)return rc;
    rc=nas_x86_qemu_xhci_release(&h13_hw);
    if(rc)return rc;
    h13_persistent.attached=false;
    nas_print("[H13 MOUSE QUIESCED RELEASED] event0 retired, xHC halted, seven DMA pages freed, 4 BAR0 UC pages unmapped\n");
    return 0;
}
