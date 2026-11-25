#ifndef NASEED_X86_QEMU_HID_INPUT_H
#define NASEED_X86_QEMU_HID_INPUT_H
#include "x86_qemu_xhci.h"
#include "../drivers/input/core.h"
#include "../drivers/usb/hid_boot.h"
#include "../drivers/usb/hid_boot_mouse.h"

/* One input object owns publication and report state. The input core's
 * registered bit is authoritative; the USB controller never shadows it. */
struct nas_x86_qemu_hid_device {
    struct nas_input_device input;
    union {
        struct nas_usb_hid_boot_keyboard keyboard;
        struct nas_usb_hid_boot_mouse mouse;
    } report;
    bool mouse;
};
static inline void nas_x86_qemu_hid_emit(void *ctx,uint16_t type,uint16_t code,int32_t value) {
    nas_input_event((struct nas_input_device *)ctx,type,code,value);
}
static inline int nas_x86_qemu_hid_publish(struct nas_x86_qemu_hid_device *hid,
                                           unsigned index,bool mouse) {
    struct nas_input_device *dev=&hid->input;
    if(dev->registered)return -EBUSY;
    hid->mouse=mouse;
    nas_input_device_init(dev,mouse?"QEMU USB HID Boot Mouse":"QEMU USB HID Boot Keyboard");
    dev->id.bustype=3U;
    if(nas_input_set_capability(dev,NAS_INPUT_EV_SYN,NAS_INPUT_SYN_REPORT))
        return -EINVAL;
    if(mouse) {
        nas_usb_hid_boot_mouse_init(&hid->report.mouse);
        if(nas_input_set_capability(dev,NAS_INPUT_EV_KEY,NAS_USB_HID_BOOT_MOUSE_BTN_LEFT) ||
           nas_input_set_capability(dev,NAS_INPUT_EV_KEY,NAS_USB_HID_BOOT_MOUSE_BTN_RIGHT) ||
           nas_input_set_capability(dev,NAS_INPUT_EV_KEY,NAS_USB_HID_BOOT_MOUSE_BTN_MIDDLE) ||
           nas_input_set_capability(dev,NAS_INPUT_EV_REL,NAS_USB_HID_BOOT_MOUSE_REL_X) ||
           nas_input_set_capability(dev,NAS_INPUT_EV_REL,NAS_USB_HID_BOOT_MOUSE_REL_Y) ||
           nas_input_set_capability(dev,NAS_INPUT_EV_REL,NAS_USB_HID_BOOT_MOUSE_REL_WHEEL))
            return -EINVAL;
    } else {
        nas_usb_hid_boot_init(&hid->report.keyboard);
        unsigned key_caps=0;
        for(unsigned u=4U;u<=0x81U;u++) {
            uint16_t key=nas_usb_hid_boot_keycode((uint8_t)u);
            if(key && !nas_input_set_capability(dev,NAS_INPUT_EV_KEY,key))key_caps++;
        }
        for(unsigned u=0xe0U;u<=0xe7U;u++) {
            uint16_t key=nas_usb_hid_boot_keycode((uint8_t)u);
            if(key && !nas_input_set_capability(dev,NAS_INPUT_EV_KEY,key))key_caps++;
        }
        if(!key_caps || !(dev->keybit[30U/8U]&(1U<<(30U%8U))))return -EINVAL;
    }
    int rc=nas_input_register_device(dev);
    if(rc)return rc;
    if(dev->index!=index) {
        rc=nas_input_unregister_device(dev);
        return rc ? rc : -EEXIST;
    }
    return 0;
}
static inline int nas_x86_qemu_hid_unpublish(struct nas_x86_qemu_hid_device *hid) {
    if(!hid->input.registered)return 0;
    if(hid->mouse)
        nas_usb_hid_boot_mouse_release_all(&hid->report.mouse,nas_x86_qemu_hid_emit,&hid->input);
    else
        nas_usb_hid_boot_release_all(&hid->report.keyboard,nas_x86_qemu_hid_emit,&hid->input);
    /* Open readers pin the node and therefore its hardware resources. */
    return nas_input_unregister_device(&hid->input);
}
typedef int (*nas_x86_qemu_hid_poll_fn)(struct nas_xhci_ro_session *,uint8_t *);
static inline int nas_x86_qemu_hid_poll(
    struct nas_xhci_ro_session *xhci,nas_x86_qemu_hid_poll_fn poll,
    struct nas_x86_qemu_hid_device *hid,unsigned budget,uint64_t *total,
    const char *rejected) {
    if(!hid->input.registered)return -ENODEV;
    if(!budget || budget>16U)return -EINVAL;
    unsigned reports=0;
    for(unsigned i=0;i<budget;i++) {
        uint8_t report[NAS_USB_HID_BOOT_REPORT_BYTES]={0};
        int rc=poll(xhci,report);
        if(rc==-EAGAIN)break;
        if(rc)return rc;
        ++reports;
        ++*total;
        rc=hid->mouse ?
            nas_usb_hid_boot_mouse_report(&hid->report.mouse,report,NAS_USB_HID_BOOT_MOUSE_REPORT_BYTES,
                                         nas_x86_qemu_hid_emit,&hid->input) :
            nas_usb_hid_boot_report(&hid->report.keyboard,report,sizeof(report),
                                   nas_x86_qemu_hid_emit,&hid->input);
        if(rc && rc!=-EAGAIN && rc!=-EPROTO)return rc;
        if(rc==-EPROTO)nas_print(rejected);
    }
    return (int)reports;
}
#endif
