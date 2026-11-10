#ifndef NASEED_USB_HID_BOOT_KEYBOARD_H
#define NASEED_USB_HID_BOOT_KEYBOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* USB-IF HID 1.11 Appendix B: 8-byte boot-keyboard input report, one
 * modifier bitmap, one OEM-reserved byte and up to six keyboard usages.
 * This is a pure decoder: it does not invoke xHCI, input/evdev, a TTY, or
 * attempt USB transfers. The owning platform calls from its deferred worker
 * and forwards EV_KEY/SYN_REPORT through nas_input_event(). */
#define NAS_USB_HID_BOOT_REPORT_BYTES 8U
#define NAS_USB_HID_BOOT_KEY_SLOTS 6U

struct nas_usb_hid_boot_keyboard {
    uint8_t previous_modifiers;
    uint8_t previous_keys[NAS_USB_HID_BOOT_KEY_SLOTS];
    bool initialized;
};

typedef void (*nas_usb_hid_boot_emit)(void *context, uint16_t type,
                                      uint16_t code, int32_t value);

/* Return Linux EV_KEY code for keyboard usage-page 0x07; zero means that
 * the usage is deliberately unsupported. Includes E0-E7 modifiers. Use it
 * when registering input capabilities, including KEY_A for console routing. */
uint16_t nas_usb_hid_boot_keycode(uint8_t usage);
void nas_usb_hid_boot_init(struct nas_usb_hid_boot_keyboard *);

/* 0: accepted, including byte-identical reports without new events.
 * -EAGAIN: hardware key rollover/error usages 01..03; hold old key state.
 * -EPROTO: nonzero reserved byte, duplicate usages or illegal modifiers;
 *          hold old key state (no ghost release or press).
 * -EINVAL: null, wrong-size or uninitialized arguments; hold old state.
 * Emit releases before new presses; emit exactly one SYN_REPORT if at
 * least one supported EV_KEY changed; never generate repeat=2 on its own. */
int nas_usb_hid_boot_report(struct nas_usb_hid_boot_keyboard *,
                            const uint8_t *report, size_t bytes,
                            nas_usb_hid_boot_emit emit, void *context);

/* Logical disconnect after transport quiesces: deliver key-up for all
 * currently held keys, and one SYN_REPORT, avoiding sticky evdev keys. */
void nas_usb_hid_boot_release_all(struct nas_usb_hid_boot_keyboard *,
                                  nas_usb_hid_boot_emit emit, void *context);

#endif
