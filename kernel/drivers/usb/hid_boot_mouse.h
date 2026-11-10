#ifndef NASEED_USB_HID_BOOT_MOUSE_H
#define NASEED_USB_HID_BOOT_MOUSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* USB HID Boot Mouse protocol (interface class 3, subclass 1, protocol 2).
 * The H13 QEMU mouse fixture reports three buttons, signed relative X/Y
 * displacements and a signed wheel delta in one 4-byte Interrupt-IN report.
 * This decoder has no dependency on the xHCI rings, evdev, or serial output.
 * The caller must accept only a qualified physical USB mouse endpoint. */
#define NAS_USB_HID_BOOT_MOUSE_REPORT_BYTES 4U
#define NAS_USB_HID_BOOT_MOUSE_BUTTON_MASK 7U

#define NAS_USB_HID_BOOT_MOUSE_BTN_LEFT 272U
#define NAS_USB_HID_BOOT_MOUSE_BTN_RIGHT 273U
#define NAS_USB_HID_BOOT_MOUSE_BTN_MIDDLE 274U
#define NAS_USB_HID_BOOT_MOUSE_REL_X 0U
#define NAS_USB_HID_BOOT_MOUSE_REL_Y 1U
#define NAS_USB_HID_BOOT_MOUSE_REL_WHEEL 8U

struct nas_usb_hid_boot_mouse {
    uint8_t previous_buttons;
    bool initialized;
};

typedef void (*nas_usb_hid_boot_mouse_emit)(void *context, uint16_t type,
                                            uint16_t code, int32_t value);

void nas_usb_hid_boot_mouse_init(struct nas_usb_hid_boot_mouse *);

/* 0 accepts an exact 4-byte hardware report, including an idle packet.
 * -EINVAL rejects null/uninitialized/wrong-size input, -EPROTO rejects
 * unsupported button bits. Failures preserve the previous button state.
 * Emit button releases before presses, then relative deltas X/Y/Wheel and
 * one SYN_REPORT if anything changed; zero values emit no relative event.
 * Repeated held-button reports do not fabricate repeat events, but two
 * nonzero relative-delta reports represent two independent movements. */
int nas_usb_hid_boot_mouse_report(struct nas_usb_hid_boot_mouse *,
                                  const uint8_t *report, size_t bytes,
                                  nas_usb_hid_boot_mouse_emit emit,
                                  void *context);

/* After transport is quiesced, release held buttons exactly once so an
 * evdev reader never retains a stuck pointer button after USB removal. */
void nas_usb_hid_boot_mouse_release_all(struct nas_usb_hid_boot_mouse *,
                                        nas_usb_hid_boot_mouse_emit emit,
                                        void *context);

#endif
