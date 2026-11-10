#include "hid_boot_mouse.h"
#include "../input/core.h"
#include "../../errno.h"

static const uint16_t boot_mouse_button_code[3] = {
    NAS_USB_HID_BOOT_MOUSE_BTN_LEFT,
    NAS_USB_HID_BOOT_MOUSE_BTN_RIGHT,
    NAS_USB_HID_BOOT_MOUSE_BTN_MIDDLE
};

void nas_usb_hid_boot_mouse_init(struct nas_usb_hid_boot_mouse *mouse) {
    if (!mouse) return;
    mouse->previous_buttons = 0;
    mouse->initialized = true;
}

static int32_t signed_delta(uint8_t byte) {
    /* Unsigned-to-signed conversion for values >=128 is implementation-
     * defined in C. Explicitly decode the USB two's-complement wire byte. */
    return byte < 128U ? (int32_t)byte : (int32_t)byte - 256;
}

int nas_usb_hid_boot_mouse_report(struct nas_usb_hid_boot_mouse *mouse,
                                  const uint8_t *report, size_t bytes,
                                  nas_usb_hid_boot_mouse_emit emit,
                                  void *context) {
    if (!mouse || !mouse->initialized || !report || !emit ||
        bytes != NAS_USB_HID_BOOT_MOUSE_REPORT_BYTES)
        return -EINVAL;
    const uint8_t buttons = report[0];
    if (buttons & (uint8_t)~NAS_USB_HID_BOOT_MOUSE_BUTTON_MASK)
        return -EPROTO;

    bool changed = false;
    const uint8_t released = (uint8_t)(mouse->previous_buttons &
                                       (uint8_t)~buttons);
    const uint8_t pressed = (uint8_t)(buttons &
                                      (uint8_t)~mouse->previous_buttons);
    for (unsigned i = 0; i < 3U; i++) {
        if (released & (1U << i)) {
            emit(context, NAS_INPUT_EV_KEY, boot_mouse_button_code[i], 0);
            changed = true;
        }
    }
    for (unsigned i = 0; i < 3U; i++) {
        if (pressed & (1U << i)) {
            emit(context, NAS_INPUT_EV_KEY, boot_mouse_button_code[i], 1);
            changed = true;
        }
    }

    const uint16_t axes[3] = {
        NAS_USB_HID_BOOT_MOUSE_REL_X,
        NAS_USB_HID_BOOT_MOUSE_REL_Y,
        NAS_USB_HID_BOOT_MOUSE_REL_WHEEL
    };
    for (unsigned i = 0; i < 3U; i++) {
        const int32_t delta = signed_delta(report[1U + i]);
        if (delta) {
            emit(context, NAS_INPUT_EV_REL, axes[i], delta);
            changed = true;
        }
    }
    mouse->previous_buttons = buttons;
    if (changed) emit(context, NAS_INPUT_EV_SYN, NAS_INPUT_SYN_REPORT, 0);
    return 0;
}

void nas_usb_hid_boot_mouse_release_all(struct nas_usb_hid_boot_mouse *mouse,
                                        nas_usb_hid_boot_mouse_emit emit,
                                        void *context) {
    if (!mouse || !mouse->initialized || !emit) return;
    const uint8_t empty[NAS_USB_HID_BOOT_MOUSE_REPORT_BYTES] = {0};
    (void)nas_usb_hid_boot_mouse_report(mouse, empty, sizeof(empty), emit,
                                       context);
}
