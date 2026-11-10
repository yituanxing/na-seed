/* H13 original hardware Boot Mouse report semantics -> Linux evdev ABI.
 * The real QEMU usb-mouse/QMP-to-original-Alpine evtest gate is separate. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../kernel/drivers/usb/hid_boot_mouse.h"
#include "../kernel/drivers/input/core.h"
#include "../kernel/drivers/input/evdev.h"
#include "../kernel/errno.h"

_Static_assert(sizeof(struct nas_input_event) == 24U,
               "evdev input_event requires Linux x86-64 LP64 24B ABI");
_Static_assert(offsetof(struct nas_input_event, type) == 16U,
               "evdev type offset");
_Static_assert(offsetof(struct nas_input_event, code) == 18U,
               "evdev code offset");
_Static_assert(offsetof(struct nas_input_event, value) == 20U,
               "evdev value offset");

static uint64_t fake_time;
uint64_t nas_arch_monotonic_ns(void) { return fake_time += 1000000U; }

struct observed {
    struct nas_input_device *device;
    struct nas_input_event events[32];
    unsigned count;
};

static void emit(void *ctx, uint16_t type, uint16_t code, int32_t value) {
    struct observed *o = ctx;
    assert(o && o->count < 32U);
    struct nas_input_event *e = &o->events[o->count++];
    e->type = type;
    e->code = code;
    e->value = value;
    nas_input_event(o->device, type, code, value);
}

static void event(const struct observed *o, unsigned i, uint16_t type,
                  uint16_t code, int32_t value) {
    assert(i < o->count);
    assert(o->events[i].type == type);
    assert(o->events[i].code == code);
    assert(o->events[i].value == value);
}

static void syn(const struct observed *o, unsigned i) {
    event(o, i, NAS_INPUT_EV_SYN, NAS_INPUT_SYN_REPORT, 0);
}

static void evdev(struct nas_evdev_client *reader, const struct observed *o) {
    struct nas_input_event got[32] = {{0}};
    if (!o->count) {
        assert(!nas_evdev_ready(reader));
        assert(nas_evdev_read(reader, got, sizeof(got)) == -EAGAIN);
        return;
    }
    assert(nas_evdev_ready(reader));
    assert(nas_evdev_read(reader, got, sizeof(got)) ==
           (int)(o->count * sizeof(got[0])));
    for (unsigned i = 0; i < o->count; i++) {
        assert(got[i].type == o->events[i].type);
        assert(got[i].code == o->events[i].code);
        assert(got[i].value == o->events[i].value);
        assert(got[i].tv_usec > 0 && got[i].tv_sec == 0);
    }
    assert(!nas_evdev_ready(reader));
}

static int report(struct nas_usb_hid_boot_mouse *mouse, struct observed *o,
                  const uint8_t bytes[4]) {
    o->count = 0;
    return nas_usb_hid_boot_mouse_report(mouse, bytes, 4, emit, o);
}

int main(void) {
    struct nas_input_device input;
    struct nas_evdev_client *reader_a = NULL, *reader_b = NULL;
    struct nas_usb_hid_boot_mouse mouse = {0};
    struct observed seen = {.device = &input};
    const uint8_t idle[4] = {0};
    const uint8_t left_down[4] = {1};
    const uint8_t left_and_xy[4] = {1,20,0xf1}; /* dx=20, dy=-15 */
    const uint8_t left_and_wheel[4] = {1,0,0,1};
    const uint8_t left_and_neg_wheel[4] = {1,0,0,255};
    const uint8_t right_middle[4] = {6};
    const uint8_t all_buttons[4] = {7};
    const uint8_t malformed[4] = {0x81,20,0,0};

    nas_input_device_init(&input, "H13 real QEMU USB Boot Mouse");
    input.id.bustype = 3; /* BUS_USB, not a synthetic/PS2 input */
    assert(nas_input_set_capability(&input, NAS_INPUT_EV_SYN,
                                    NAS_INPUT_SYN_REPORT) == 0);
    for (unsigned c = 272; c <= 274; c++)
        assert(nas_input_set_capability(&input, NAS_INPUT_EV_KEY, c) == 0);
    assert(nas_input_set_capability(&input, NAS_INPUT_EV_REL, 0) == 0);
    assert(nas_input_set_capability(&input, NAS_INPUT_EV_REL, 1) == 0);
    assert(nas_input_set_capability(&input, NAS_INPUT_EV_REL, 8) == 0);
    assert(nas_input_register_device(&input) == 0 && input.index == 0);
    assert(nas_evdev_open(0, &reader_a) == 0);
    assert(nas_evdev_open(0, &reader_b) == 0);

    /* No input publication prior to initialization, or on null, short,
     * long, unsupported button-bit reports. */
    assert(report(&mouse, &seen, left_down) == -EINVAL && !seen.count);
    nas_usb_hid_boot_mouse_init(&mouse);
    assert(report(&mouse, &seen, idle) == 0 && !seen.count);
    assert(nas_usb_hid_boot_mouse_report(&mouse, NULL, 4, emit,
                                         &seen) == -EINVAL);
    assert(nas_usb_hid_boot_mouse_report(&mouse, idle, 3, emit,
                                         &seen) == -EINVAL);
    assert(nas_usb_hid_boot_mouse_report(&mouse, idle, 5, emit,
                                         &seen) == -EINVAL);
    assert(nas_usb_hid_boot_mouse_report(&mouse, idle, 4, NULL,
                                         &seen) == -EINVAL);
    assert(nas_usb_hid_boot_mouse_report(NULL, idle, 4, emit,
                                         &seen) == -EINVAL);
    assert(!seen.count);
    evdev(reader_a, &seen);

    assert(report(&mouse, &seen, left_down) == 0 && seen.count == 2);
    event(&seen, 0, NAS_INPUT_EV_KEY, 272, 1);
    syn(&seen, 1);
    evdev(reader_a, &seen);
    evdev(reader_b, &seen);
    assert(input.key_state[272/8] & (1U << (272%8)));
    assert(report(&mouse, &seen, left_down) == 0 && !seen.count);
    evdev(reader_a, &seen);

    /* A malformed high button bit cannot generate ghost releases or
     * movement and must retain the held left button until valid release. */
    assert(report(&mouse, &seen, malformed) == -EPROTO && !seen.count);
    assert(input.key_state[272/8] & (1U << (272%8)));
    evdev(reader_a, &seen);

    assert(report(&mouse, &seen, left_and_xy) == 0 && seen.count == 3);
    event(&seen, 0, NAS_INPUT_EV_REL, 0, 20);
    event(&seen, 1, NAS_INPUT_EV_REL, 1, -15);
    syn(&seen, 2);
    evdev(reader_a, &seen);
    evdev(reader_b, &seen);
    /* Relative reports are independent deltas even when byte-identical;
     * held buttons still must not auto-repeat as EV_KEY value=2. */
    assert(report(&mouse, &seen, left_and_xy) == 0 && seen.count == 3);
    event(&seen, 0, NAS_INPUT_EV_REL, 0, 20);
    event(&seen, 1, NAS_INPUT_EV_REL, 1, -15);
    syn(&seen, 2);
    evdev(reader_a, &seen);
    evdev(reader_b, &seen);

    assert(report(&mouse, &seen, left_and_wheel) == 0 && seen.count == 2);
    event(&seen, 0, NAS_INPUT_EV_REL, 8, 1);
    syn(&seen, 1);
    evdev(reader_a, &seen);
    evdev(reader_b, &seen);
    assert(report(&mouse, &seen, left_and_neg_wheel) == 0 &&
           seen.count == 2);
    event(&seen, 0, NAS_INPUT_EV_REL, 8, -1);
    syn(&seen, 1);
    evdev(reader_a, &seen);
    evdev(reader_b, &seen);

    assert(report(&mouse, &seen, right_middle) == 0 && seen.count == 4);
    event(&seen, 0, NAS_INPUT_EV_KEY, 272, 0);
    event(&seen, 1, NAS_INPUT_EV_KEY, 273, 1);
    event(&seen, 2, NAS_INPUT_EV_KEY, 274, 1);
    syn(&seen, 3);
    evdev(reader_a, &seen);
    evdev(reader_b, &seen);
    assert(report(&mouse, &seen, all_buttons) == 0 && seen.count == 2);
    event(&seen, 0, NAS_INPUT_EV_KEY, 272, 1);
    syn(&seen, 1);
    evdev(reader_a, &seen);
    evdev(reader_b, &seen);

    /* USB disconnect forces one synchronized release of all held buttons.
     * A repeated teardown cannot leak duplicate state transitions. */
    seen.count = 0;
    nas_usb_hid_boot_mouse_release_all(&mouse, emit, &seen);
    assert(seen.count == 4);
    event(&seen, 0, NAS_INPUT_EV_KEY, 272, 0);
    event(&seen, 1, NAS_INPUT_EV_KEY, 273, 0);
    event(&seen, 2, NAS_INPUT_EV_KEY, 274, 0);
    syn(&seen, 3);
    evdev(reader_a, &seen);
    evdev(reader_b, &seen);
    seen.count = 0;
    nas_usb_hid_boot_mouse_release_all(&mouse, emit, &seen);
    assert(!seen.count);
    evdev(reader_a, &seen);
    assert(!input.key_state[272/8] && !input.key_state[273/8]);

    assert(nas_input_unregister_device(&input) == -EBUSY);
    nas_evdev_close(reader_a);
    nas_evdev_close(reader_b);
    assert(nas_input_unregister_device(&input) == 0);
    puts("[H13 HID MOUSE HOST PASS] original 4B Boot Mouse BTN_LEFT/RIGHT/MIDDLE, signed REL_X/Y/WHEEL, per-report SYN, evdev 24B fan-out, malformed fail-closed, disconnect release");
    return 0;
}
