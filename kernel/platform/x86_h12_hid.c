#include "x86_h12_hid.h"
#include "x86_qemu_xhci.h"
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
static struct nas_usb_hid_boot_keyboard h12_report_state;
static struct nas_input_device h12_input;
static struct {
    bool attached,input_published;
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
    rc=nas_x86_qemu_xhci_release(&h12_hw);
    if(rc)return rc;
    h12_persistent.attached=false;
    nas_print("[H12 HID QUIESCED RELEASED] event0 retired, xHC halted, seven DMA pages freed, 4 BAR0 UC pages unmapped\n");
    return 0;
}
