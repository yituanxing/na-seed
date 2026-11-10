/* H12 independent adversarial test: genuine HID decoder -> input core ->
 * independent Linux evdev descriptions. Real xHCI/QMP is a separate gate. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../kernel/drivers/usb/hid_boot.h"
#include "../kernel/drivers/input/core.h"
#include "../kernel/drivers/input/evdev.h"
#include "../kernel/errno.h"

_Static_assert(sizeof(struct nas_input_event) == 24U,
               "Linux x86-64 input_event must use 24-byte LP64 ABI");
_Static_assert(offsetof(struct nas_input_event, type) == 16U,
               "Linux input_event.type offset must be 16");
_Static_assert(offsetof(struct nas_input_event, code) == 18U,
               "Linux input_event.code offset must be 18");
_Static_assert(offsetof(struct nas_input_event, value) == 20U,
               "Linux input_event.value offset must be 20");

static uint64_t monotonic_ns;
uint64_t nas_arch_monotonic_ns(void) { return monotonic_ns += 1000000U; }

struct observed {
    struct nas_input_device *device;
    struct nas_input_event events[32];
    unsigned count;
};

static void record_event(void *opaque, uint16_t type, uint16_t code,
                         int32_t value) {
    struct observed *o = opaque;
    assert(o && o->count < sizeof(o->events) / sizeof(o->events[0]));
    struct nas_input_event *e = &o->events[o->count++];
    e->type = type;
    e->code = code;
    e->value = value;
    nas_input_event(o->device, type, code, value);
}

static void expect_event(const struct observed *o, unsigned index,
                          uint16_t type, uint16_t code, int value) {
    assert(index < o->count);
    assert(o->events[index].type == type);
    assert(o->events[index].code == code);
    assert(o->events[index].value == value);
}

static void expect_syn(const struct observed *o, unsigned index) {
    expect_event(o, index, NAS_INPUT_EV_SYN, NAS_INPUT_SYN_REPORT, 0);
}

static void expect_evdev(struct nas_evdev_client *client,
                         const struct observed *o) {
    struct nas_input_event got[32] = {{0}};
    if (!o->count) {
        assert(!nas_evdev_ready(client));
        assert(nas_evdev_read(client, got, sizeof(got)) == -EAGAIN);
        return;
    }
    assert(nas_evdev_ready(client));
    assert(nas_evdev_read(client, got, sizeof(got)) ==
           (int)(o->count * sizeof(got[0])));
    for (unsigned i = 0; i < o->count; i++) {
        assert(got[i].type == o->events[i].type);
        assert(got[i].code == o->events[i].code);
        assert(got[i].value == o->events[i].value);
        assert(got[i].tv_sec == 0 && got[i].tv_usec > 0);
    }
    assert(!nas_evdev_ready(client));
}

static int report(struct nas_usb_hid_boot_keyboard *kbd, struct observed *o,
                  const uint8_t bytes[8]) {
    o->count = 0;
    return nas_usb_hid_boot_report(kbd, bytes, 8, record_event, o);
}

int main(void) {
    struct nas_usb_hid_boot_keyboard keyboard = {0};
    struct nas_input_device input;
    struct observed seen = { .device = &input };
    struct nas_evdev_client *reader_a = NULL, *reader_b = NULL;

    nas_input_device_init(&input, "H12 QEMU USB Boot Keyboard");
    assert(nas_usb_hid_boot_keycode(0x04) == 30); /* A */
    assert(nas_usb_hid_boot_keycode(0x05) == 48); /* B */
    assert(nas_usb_hid_boot_keycode(0xe1) == 42); /* LSHIFT */
    assert(nas_usb_hid_boot_keycode(0x00) == 0);
    assert(nas_input_set_capability(&input, NAS_INPUT_EV_KEY, 30) == 0);
    assert(nas_input_set_capability(&input, NAS_INPUT_EV_KEY, 48) == 0);
    assert(nas_input_set_capability(&input, NAS_INPUT_EV_KEY, 42) == 0);
    assert(nas_input_register_device(&input) == 0 && input.index == 0);
    assert(nas_evdev_open(0, &reader_a) == 0);
    assert(nas_evdev_open(0, &reader_b) == 0);

    const uint8_t idle[8] = {0};
    const uint8_t a_down[8] = {0,0,0x04};
    const uint8_t b_down[8] = {0,0,0x05};
    const uint8_t shift_a[8] = {0x02,0,0x04};
    const uint8_t shift_only[8] = {0x02,0};
    const uint8_t rollover[8] = {0,0,0x01};
    const uint8_t post_fail[8] = {0,0,0x02};
    const uint8_t nonzero_reserved[8] = {0,0x7f};
    const uint8_t duplicate_a[8] = {0,0,0x04,0x04};
    const uint8_t modifier_in_key_slot[8] = {0,0,0xe1};

    /* Uninitialized must reject without calling into the input pipeline. */
    assert(report(&keyboard, &seen, a_down) == -EINVAL && !seen.count);
    nas_usb_hid_boot_init(&keyboard);
    assert(report(&keyboard, &seen, idle) == 0 && !seen.count);
    expect_evdev(reader_a, &seen);

    /* Actual QMP A press, duplicate polling report, then release. */
    assert(report(&keyboard, &seen, a_down) == 0 && seen.count == 2);
    expect_event(&seen, 0, NAS_INPUT_EV_KEY, 30, 1);
    expect_syn(&seen, 1);
    expect_evdev(reader_a, &seen);
    expect_evdev(reader_b, &seen); /* fan-out, not a destructive single FIFO */
    assert((input.key_state[30/8] & (1U << (30%8))) != 0);
    assert(report(&keyboard, &seen, a_down) == 0 && seen.count == 0);
    expect_evdev(reader_a, &seen); /* no fabricated repeat=2 */

    /* ErrorRollOver/POSTFail and malformed transfer cannot release held A. */
    assert(report(&keyboard, &seen, rollover) == -EAGAIN && !seen.count);
    assert(report(&keyboard, &seen, post_fail) == -EAGAIN && !seen.count);
    assert(report(&keyboard, &seen, nonzero_reserved) == -EPROTO && !seen.count);
    assert(report(&keyboard, &seen, duplicate_a) == -EPROTO && !seen.count);
    assert(report(&keyboard, &seen, modifier_in_key_slot) == -EPROTO && !seen.count);
    assert(nas_usb_hid_boot_report(&keyboard, idle, 7, record_event,
                                   &seen) == -EINVAL);
    assert(nas_usb_hid_boot_report(&keyboard, idle, 9, record_event,
                                   &seen) == -EINVAL);
    assert(nas_usb_hid_boot_report(&keyboard, NULL, 8, record_event,
                                   &seen) == -EINVAL);
    assert(nas_usb_hid_boot_report(&keyboard, idle, 8, NULL,
                                   &seen) == -EINVAL);
    assert(!seen.count);
    assert((input.key_state[30/8] & (1U << (30%8))) != 0);
    expect_evdev(reader_a, &seen);

    assert(report(&keyboard, &seen, idle) == 0 && seen.count == 2);
    expect_event(&seen, 0, NAS_INPUT_EV_KEY, 30, 0);
    expect_syn(&seen, 1);
    expect_evdev(reader_a, &seen);
    expect_evdev(reader_b, &seen);

    /* Shift must precede A in the packet; changing key releases old key
     * before pressing new key while preserving exactly one SYN_REPORT. */
    assert(report(&keyboard, &seen, shift_a) == 0 && seen.count == 3);
    expect_event(&seen, 0, NAS_INPUT_EV_KEY, 42, 1);
    expect_event(&seen, 1, NAS_INPUT_EV_KEY, 30, 1);
    expect_syn(&seen, 2);
    expect_evdev(reader_a, &seen);
    expect_evdev(reader_b, &seen);

    assert(report(&keyboard, &seen, shift_only) == 0 && seen.count == 2);
    expect_event(&seen, 0, NAS_INPUT_EV_KEY, 30, 0);
    expect_syn(&seen, 1);
    expect_evdev(reader_a, &seen);
    expect_evdev(reader_b, &seen);

    assert(report(&keyboard, &seen, b_down) == 0 && seen.count == 3);
    expect_event(&seen, 0, NAS_INPUT_EV_KEY, 42, 0);
    expect_event(&seen, 1, NAS_INPUT_EV_KEY, 48, 1);
    expect_syn(&seen, 2);
    expect_evdev(reader_a, &seen);
    expect_evdev(reader_b, &seen);

    /* USB disconnect after B held clears evdev key state exactly once. */
    seen.count = 0;
    nas_usb_hid_boot_release_all(&keyboard, record_event, &seen);
    assert(seen.count == 2);
    expect_event(&seen, 0, NAS_INPUT_EV_KEY, 48, 0);
    expect_syn(&seen, 1);
    expect_evdev(reader_a, &seen);
    expect_evdev(reader_b, &seen);
    seen.count = 0;
    nas_usb_hid_boot_release_all(&keyboard, record_event, &seen);
    assert(seen.count == 0);
    expect_evdev(reader_a, &seen);

    assert(nas_input_unregister_device(&input) == -EBUSY);
    nas_evdev_close(reader_a);
    nas_evdev_close(reader_b);
    assert(nas_input_unregister_device(&input) == 0);
    puts("[H12 HID HOST PASS] genuine boot report parser, A down/up, modifiers, rollover, malformed reports, 24B evdev fan-out, disconnect release");
    return 0;
}
