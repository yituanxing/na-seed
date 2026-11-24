#ifndef NASEED_X86_QEMU_HID_INPUT_H
#define NASEED_X86_QEMU_HID_INPUT_H
#include "x86_qemu_xhci.h"
#include "../drivers/input/core.h"
#include "../drivers/usb/hid_boot.h"
#include "../drivers/usb/hid_boot_mouse.h"

/* These are the exact Linux capabilities of the qualified QEMU boot
 * protocol devices. Registration remains deferred until input core init. */
static inline int nas_x86_qemu_hid_keyboard_register(struct nas_input_device *dev,
                                                     unsigned index) {
    nas_input_device_init(dev,"QEMU USB HID Boot Keyboard");
    dev->id.bustype=3U;
    if(nas_input_set_capability(dev,NAS_INPUT_EV_SYN,NAS_INPUT_SYN_REPORT))
        return -EINVAL;
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
    int rc=nas_input_register_device(dev);
    if(rc)return rc;
    if(dev->index!=index) {
        (void)nas_input_unregister_device(dev);
        return -EEXIST;
    }
    return 0;
}
static inline int nas_x86_qemu_hid_mouse_register(struct nas_input_device *dev,
                                                  unsigned index) {
    nas_input_device_init(dev,"QEMU USB HID Boot Mouse");
    dev->id.bustype=3U;
    if(nas_input_set_capability(dev,NAS_INPUT_EV_SYN,NAS_INPUT_SYN_REPORT) ||
       nas_input_set_capability(dev,NAS_INPUT_EV_KEY,NAS_USB_HID_BOOT_MOUSE_BTN_LEFT) ||
       nas_input_set_capability(dev,NAS_INPUT_EV_KEY,NAS_USB_HID_BOOT_MOUSE_BTN_RIGHT) ||
       nas_input_set_capability(dev,NAS_INPUT_EV_KEY,NAS_USB_HID_BOOT_MOUSE_BTN_MIDDLE) ||
       nas_input_set_capability(dev,NAS_INPUT_EV_REL,NAS_USB_HID_BOOT_MOUSE_REL_X) ||
       nas_input_set_capability(dev,NAS_INPUT_EV_REL,NAS_USB_HID_BOOT_MOUSE_REL_Y) ||
       nas_input_set_capability(dev,NAS_INPUT_EV_REL,NAS_USB_HID_BOOT_MOUSE_REL_WHEEL))
        return -EINVAL;
    int rc=nas_input_register_device(dev);
    if(rc)return rc;
    if(dev->index!=index) {
        (void)nas_input_unregister_device(dev);
        return -EEXIST;
    }
    return 0;
}
static inline void nas_x86_qemu_hid_emit(void *ctx,uint16_t type,uint16_t code,int32_t value) {
    nas_input_event((struct nas_input_device *)ctx,type,code,value);
}
typedef int (*nas_x86_qemu_hid_poll_fn)(struct nas_xhci_ro_session *,uint8_t *);
static inline int nas_x86_qemu_hid_keyboard_poll(
    struct nas_xhci_ro_session *xhci,nas_x86_qemu_hid_poll_fn poll,
    struct nas_usb_hid_boot_keyboard *state,struct nas_input_device *dev,
    unsigned budget,uint64_t *total,const char *rejected) {
    unsigned reports=0;
    for(unsigned i=0;i<budget;i++) {
        uint8_t report[NAS_USB_HID_BOOT_REPORT_BYTES]={0};
        int rc=poll(xhci,report);
        if(rc==-EAGAIN)break;
        if(rc)return rc;
        ++reports;
        ++*total;
        rc=nas_usb_hid_boot_report(state,report,sizeof(report),nas_x86_qemu_hid_emit,dev);
        if(rc && rc!=-EAGAIN && rc!=-EPROTO)return rc;
        if(rc==-EPROTO)nas_print(rejected);
    }
    return (int)reports;
}
static inline int nas_x86_qemu_hid_mouse_poll(
    struct nas_xhci_ro_session *xhci,nas_x86_qemu_hid_poll_fn poll,
    struct nas_usb_hid_boot_mouse *state,struct nas_input_device *dev,
    unsigned budget,uint64_t *total,const char *rejected) {
    unsigned reports=0;
    for(unsigned i=0;i<budget;i++) {
        uint8_t report[NAS_USB_HID_BOOT_MOUSE_REPORT_BYTES]={0};
        int rc=poll(xhci,report);
        if(rc==-EAGAIN)break;
        if(rc)return rc;
        ++reports;
        ++*total;
        rc=nas_usb_hid_boot_mouse_report(state,report,sizeof(report),nas_x86_qemu_hid_emit,dev);
        if(rc && rc!=-EAGAIN && rc!=-EPROTO)return rc;
        if(rc==-EPROTO)nas_print(rejected);
    }
    return (int)reports;
}
#endif
