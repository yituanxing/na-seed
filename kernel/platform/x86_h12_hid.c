#include "x86_h12_hid.h"
#include "x86_qemu_hid_input.h"
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
#define H12_XHCI_VIRT_BASE UINT64_C(0xfffffe8001800000)

static struct nas_x86_qemu_xhci h12_hw={.mmio={H12_XHCI_VIRT_BASE,NAS_H11_XHCI_MMIO_BYTES}};
static struct nas_xhci_ro_session h12_xhci;
static struct nas_x86_qemu_hid_device h12_input;
static struct {
    bool attached;
    uint64_t reports;
} h12_persistent;

int nas_x86_h12_hid_bind(const struct nas_pci_function_info *fn,
                         const struct nas_pci_resources *resources) {
    uint64_t flags=0;
    int rc=nas_x86_qemu_xhci_prepare(&h12_hw,fn,resources,&flags,
        "[H12 HID FAIL] unqualified QEMU PCI xHCI\n",
        "[H12 HID FAIL] unqualified MEM64 BAR0; 16MiB Q35 high whitelist only\n",
        "[H12 HID BAR QUALIFIED] QEMU 00:04.0 1b36:000d MEM64="," 16KiB UC/NX\n");
    if(rc)return rc;
    rc=nas_xhci_hid_open(&h12_hw.io,&nas_dma_qemu_coherent,&h12_xhci);
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
        } else {
            (void)nas_x86_qemu_xhci_release(&h12_hw);
        }
        nas_x86_qemu_restore_if(flags);
        nas_print("[H12 HID FAIL] required QEMU USB Boot Keyboard unavailable\n");
        return rc;
    }
    h12_persistent.attached=true;
    nas_x86_qemu_restore_if(flags);
    nas_print("[H12 HID USB ENUMERATED] 8-byte Boot Keyboard interrupt-IN; input class publication deferred\n");
    return 0;
}

int nas_x86_h12_hid_publish_input(void) {
    if(!h12_persistent.attached || h12_input.input.registered)
        return -ENODEV;
    int rc=nas_x86_qemu_hid_publish(&h12_input,0U,false);
    if(rc)return rc;
    nas_print("[H12 HID INPUT ATTACHED] /dev/input/event0 QEMU USB Boot Keyboard EV_KEY/SYN_REPORT\n");
    return 0;
}

int nas_x86_h12_hid_poll(unsigned budget) {
    if(!h12_persistent.attached || !h12_input.input.registered)
        return -ENODEV;
    return nas_x86_qemu_hid_poll(&h12_xhci,nas_xhci_hid_poll,
        &h12_input,budget,&h12_persistent.reports,"[H12 HID REPORT REJECTED] malformed Boot Keyboard payload\n");
}

int nas_x86_h12_hid_shutdown(void) {
    if(!h12_persistent.attached)return -ENODEV;
    int rc=nas_x86_qemu_hid_unpublish(&h12_input);
    if(rc)return rc;
    rc=nas_xhci_hid_close(&h12_xhci);
    if(rc)return rc; /* Keep 7 DMA leases and BAR if xHC won't halt. */
    rc=nas_x86_qemu_xhci_release(&h12_hw);
    if(rc)return rc;
    h12_persistent.attached=false;
    nas_print("[H12 HID QUIESCED RELEASED] event0 retired, xHC halted, seven DMA pages freed, 4 BAR0 UC pages unmapped\n");
    return 0;
}
