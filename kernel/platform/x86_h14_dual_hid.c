#include "x86_h14_dual_hid.h"
#include "x86_qemu_hid_input.h"
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
#define H14_XHCI_VIRT_BASE UINT64_C(0xfffffe8001800000)

static struct nas_x86_qemu_xhci h14_hw={.mmio={H14_XHCI_VIRT_BASE,NAS_H11_XHCI_MMIO_BYTES}};
static struct nas_xhci_ro_session h14_xhci;
static struct nas_usb_hid_boot_mouse h14_mouse_state;
static struct nas_usb_hid_boot_keyboard h14_keyboard_state;
static struct nas_input_device h14_mouse_input;
static struct nas_input_device h14_keyboard_input;
static struct {
    bool attached,input_published;
    bool keyboard_published,mouse_published;
    uint64_t reports;
} h14_persistent;

int nas_x86_h14_dual_hid_bind(const struct nas_pci_function_info *fn,
                           const struct nas_pci_resources *resources) {
    uint64_t flags=0;
    int rc=nas_x86_qemu_xhci_prepare(&h14_hw,fn,resources,&flags,
        "[H14 DUAL FAIL] unqualified QEMU PCI xHCI\n",
        "[H14 DUAL FAIL] unqualified MEM64 BAR0; 16MiB Q35 high whitelist only\n",
        "[H14 DUAL BAR QUALIFIED] QEMU 00:04.0 1b36:000d MEM64="," 16KiB UC/NX\n");
    if(rc)return rc;
    rc=nas_xhci_dual_hid_open(&h14_hw.io,&nas_dma_qemu_coherent,&h14_xhci);
    if(rc) {
        nas_print("[H14 DUAL FAIL DIAG] stage=");
        nas_dec((unsigned)h14_xhci.debug_stage);
        nas_print(" rc=");nas_dec((unsigned)(rc<0?-rc:rc));
        nas_print(" cc=");nas_dec((unsigned)h14_xhci.debug_completion_code);
        nas_print("\n");
        const uint8_t missing=h14_xhci.dual_missing_protocol;
        if(nas_xhci_dual_hid_close(&h14_xhci)) {
            nas_print("[H14 DUAL PINNED] xHC halt unproven: DMA/BAR retained\n");
        } else {
            (void)nas_x86_qemu_xhci_release(&h14_hw);
        }
        nas_x86_qemu_restore_if(flags);
        if(missing==1U)
            nas_print("[H14 DUAL FAIL] required USB Boot Keyboard unavailable\n");
        else if(missing==2U)
            nas_print("[H14 DUAL FAIL] required USB Boot Mouse unavailable\n");
        else
            nas_print("[H14 DUAL FAIL] shared xHCI controller initialization failed\n");
        return rc;
    }
    h14_persistent.attached=true;
    nas_x86_qemu_restore_if(flags);
    nas_print("[H14 DUAL USB ENUMERATED] same QEMU PCI xHCI, two physical root ports; 8B Boot Keyboard and 4B Boot Mouse Interrupt-IN\n");
    return 0;
}

int nas_x86_h14_dual_hid_publish_input(void) {
    if(!h14_persistent.attached || h14_persistent.input_published ||
       h14_persistent.keyboard_published || h14_persistent.mouse_published)
        return -ENODEV;
    nas_usb_hid_boot_init(&h14_keyboard_state);
    nas_usb_hid_boot_mouse_init(&h14_mouse_state);
    int rc=nas_x86_qemu_hid_keyboard_register(&h14_keyboard_input,0U);
    if(rc)return rc;
    h14_persistent.keyboard_published=true;

    rc=nas_x86_qemu_hid_mouse_register(&h14_mouse_input,1U);
    if(rc)goto rollback_keyboard;
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

int nas_x86_h14_dual_hid_poll(unsigned budget) {
    if(!h14_persistent.attached || !h14_persistent.input_published)
        return -ENODEV;
    if(!budget || budget>16U)return -EINVAL;
    int keyboard=nas_x86_qemu_hid_keyboard_poll(&h14_xhci,nas_xhci_dual_hid_poll_keyboard,
        &h14_keyboard_state,&h14_keyboard_input,budget,&h14_persistent.reports,"[H14 DUAL KBD REPORT REJECTED] malformed report\n");
    if(keyboard<0)return keyboard;
    int mouse=nas_x86_qemu_hid_mouse_poll(&h14_xhci,nas_xhci_dual_hid_poll_mouse,
        &h14_mouse_state,&h14_mouse_input,budget,&h14_persistent.reports,"[H14 DUAL MOUSE REPORT REJECTED] malformed report\n");
    if(mouse<0)return mouse;
    return keyboard+mouse;
}

int nas_x86_h14_dual_hid_shutdown(void) {
    if(!h14_persistent.attached)return -ENODEV;
    if(h14_persistent.mouse_published) {
        nas_usb_hid_boot_mouse_release_all(&h14_mouse_state,nas_x86_qemu_hid_emit,
                                            &h14_mouse_input);
        int rc=nas_input_unregister_device(&h14_mouse_input);
        if(rc)return rc; /* Live evdev readers pin DMA/BAR. */
        h14_persistent.mouse_published=false;
    }
    if(h14_persistent.keyboard_published) {
        nas_usb_hid_boot_release_all(&h14_keyboard_state,nas_x86_qemu_hid_emit,
                                      &h14_keyboard_input);
        int rc=nas_input_unregister_device(&h14_keyboard_input);
        if(rc)return rc;
        h14_persistent.keyboard_published=false;
    }
    h14_persistent.input_published=false;
    int rc=nas_xhci_dual_hid_close(&h14_xhci);
    if(rc)return rc; /* Unsafe DMA stop: keep BAR pinned. */
    rc=nas_x86_qemu_xhci_release(&h14_hw);
    if(rc)return rc;
    h14_persistent.attached=false;
    nas_print("[H14 DUAL QUIESCED RELEASED] event0/event1 retired, xHC halted, DMA leases freed, 4 BAR0 UC pages unmapped\n");
    return 0;
}
