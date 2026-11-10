/* SPDX-License-Identifier: GPL-2.0-only */
/* Input-device state and handler boundary ported from the Linux input model
 * (drivers/input/input.c); NaSeed keeps only capabilities required by its
 * actual guests, without importing Linux's device/kobject framework. */
#include "core.h"
#include "evdev.h"
#include "../../arch/arch.h"
#include "../../errno.h"
#include "../../lib/string.h"

/* Linux-style device/handler split: the concrete input driver owns storage,
 * while the core owns only stable device indices and capability/state checks.
 * Slot 0 is event0, not an assumption that it is a keyboard or a mouse. */
static struct nas_input_device *devices[NAS_INPUT_MAX_DEVICES];
static int (*publish_node)(unsigned index,bool add);
static void (*keyboard_handler)(uint16_t code,bool pressed);

int nas_input_set_keyboard_handler(void (*handler)(uint16_t code,bool pressed)) {
    if (!handler) return -EINVAL;
    if (keyboard_handler) return -EBUSY;
    keyboard_handler=handler;
    return 0;
}

int nas_input_set_publication(int (*publish)(unsigned index,bool add)) {
    if(!publish || publish_node)return -EINVAL;
    for(unsigned i=0;i<NAS_INPUT_MAX_DEVICES;i++)
        if(devices[i])return -EBUSY;
    publish_node=publish;
    return 0;
}

static bool bit_test(const uint8_t *bitmap, unsigned code) {
    return (bitmap[code / 8U] & (uint8_t)(1U << (code & 7U))) != 0;
}
static void bit_set(uint8_t *bitmap, unsigned code) {
    bitmap[code / 8U] |= (uint8_t)(1U << (code & 7U));
}
static void bit_clear(uint8_t *bitmap, unsigned code) {
    bitmap[code / 8U] &= (uint8_t)~(1U << (code & 7U));
}

void nas_input_device_init(struct nas_input_device *dev, const char *name) {
    if (!dev) return;
    memset(dev, 0, sizeof(*dev));
    dev->index = NAS_INPUT_MAX_DEVICES;
    dev->evbit = 1U << NAS_INPUT_EV_SYN;
    if (name) {
        size_t n = strnlen(name, NAS_INPUT_NAME_BYTES - 1U);
        memcpy(dev->name, name, n);
    }
}

static uint8_t *mutable_bits(struct nas_input_device *dev,
                             uint16_t type, unsigned *maxcode) {
    switch (type) {
    case NAS_INPUT_EV_KEY: *maxcode = NAS_INPUT_KEY_MAX; return dev->keybit;
    case NAS_INPUT_EV_REL: *maxcode = NAS_INPUT_REL_MAX; return dev->relbit;
    case NAS_INPUT_EV_ABS: *maxcode = NAS_INPUT_ABS_MAX; return dev->absbit;
    case NAS_INPUT_EV_MSC: *maxcode = 0x07U; return dev->mscbit;
    case NAS_INPUT_EV_SW:  *maxcode = 0x10U; return dev->swbit;
    case NAS_INPUT_EV_LED: *maxcode = 0x0fU; return dev->ledbit;
    case NAS_INPUT_EV_SND: *maxcode = 0x07U; return dev->sndbit;
    case NAS_INPUT_EV_REP: *maxcode = 0x01U; return dev->repbit;
    default: return NULL;
    }
}

int nas_input_set_capability(struct nas_input_device *dev,
                             uint16_t type, uint16_t code) {
    if (!dev || dev->registered) return -EINVAL;
    if (type == NAS_INPUT_EV_SYN)
        return code <= NAS_INPUT_SYN_DROPPED ? 0 : -EINVAL;
    if (type >= 32U) return -EINVAL;
    unsigned maxcode = 0;
    uint8_t *bits = mutable_bits(dev, type, &maxcode);
    if (!bits || code > maxcode) return -EINVAL;
    bit_set(bits, code);
    dev->evbit |= (uint32_t)1U << type;
    return 0;
}

int nas_input_set_property(struct nas_input_device *dev, uint16_t code) {
    if (!dev || dev->registered || code > NAS_INPUT_PROP_MAX)
        return -EINVAL;
    bit_set(dev->propbit, code);
    return 0;
}

int nas_input_set_absinfo(struct nas_input_device *dev, uint16_t code,
                          int32_t min, int32_t max, int32_t fuzz,
                          int32_t flat, int32_t resolution) {
    if (!dev || dev->registered || code > NAS_INPUT_ABS_MAX ||
        min > max || fuzz < 0 || flat < 0 || resolution < 0)
        return -EINVAL;
    struct nas_input_absinfo *a = &dev->absinfo[code];
    a->minimum = min;
    a->maximum = max;
    a->fuzz = fuzz;
    a->flat = flat;
    a->resolution = resolution;
    return nas_input_set_capability(dev, NAS_INPUT_EV_ABS, code);
}

int nas_input_register_device(struct nas_input_device *dev) {
    if (!dev || dev->registered || dev->readers || !dev->name[0])
        return -EINVAL;
    /* Do not publish an event interface for an empty/identity-only device. */
    if (!(dev->evbit & ~(1U << NAS_INPUT_EV_SYN))) return -EINVAL;
    for (unsigned slot = 0; slot < NAS_INPUT_MAX_DEVICES; ++slot) {
        if (!devices[slot]) {
            dev->index = slot;
            dev->registered = true;
            devices[slot] = dev;
            int rc=publish_node ? publish_node(slot,true) : 0;
            if(rc) {
                devices[slot]=NULL;
                dev->registered=false;
                dev->index=NAS_INPUT_MAX_DEVICES;
                return rc;
            }
            return 0;
        }
    }
    return -ENOSPC;
}

int nas_input_unregister_device(struct nas_input_device *dev) {
    if (!dev || !dev->registered || dev->index >= NAS_INPUT_MAX_DEVICES ||
        devices[dev->index] != dev)
        return -ENODEV;
    if (dev->readers) return -EBUSY;
    if(publish_node) {
        int rc=publish_node(dev->index,false);
        if(rc)return rc;
    }
    devices[dev->index] = NULL;
    dev->registered = false;
    dev->index = NAS_INPUT_MAX_DEVICES;
    return 0;
}

const struct nas_input_device *nas_input_device_at(unsigned index) {
    return index < NAS_INPUT_MAX_DEVICES ? devices[index] : NULL;
}

const uint8_t *nas_input_capabilities(const struct nas_input_device *dev,
                                      uint16_t type, size_t *bytes) {
    if (bytes) *bytes = 0;
    if (!dev || !bytes) return NULL;
    switch (type) {
    case NAS_INPUT_EV_SYN:
        *bytes = sizeof(dev->evbit); return (const uint8_t *)&dev->evbit;
    case NAS_INPUT_EV_KEY:
        *bytes = sizeof(dev->keybit); return dev->keybit;
    case NAS_INPUT_EV_REL:
        *bytes = sizeof(dev->relbit); return dev->relbit;
    case NAS_INPUT_EV_ABS:
        *bytes = sizeof(dev->absbit); return dev->absbit;
    case NAS_INPUT_EV_MSC:
        *bytes = sizeof(dev->mscbit); return dev->mscbit;
    case NAS_INPUT_EV_SW:
        *bytes = sizeof(dev->swbit); return dev->swbit;
    case NAS_INPUT_EV_LED:
        *bytes = sizeof(dev->ledbit); return dev->ledbit;
    case NAS_INPUT_EV_SND:
        *bytes = sizeof(dev->sndbit); return dev->sndbit;
    case NAS_INPUT_EV_REP:
        *bytes = sizeof(dev->repbit); return dev->repbit;
    default: return NULL;
    }
}

void nas_input_event(struct nas_input_device *dev, uint16_t type,
                     uint16_t code, int32_t value) {
    if (!dev || !dev->registered || type >= 32U ||
        !(dev->evbit & ((uint32_t)1U << type))) return;
    if (type == NAS_INPUT_EV_SYN) {
        /* EV_SYN is not state; Linux reserves SYN_DROPPED for evdev loss. */
        if (code != NAS_INPUT_SYN_REPORT && code != 2U && code != 1U)
            return;
    } else {
        unsigned maxcode = 0;
        uint8_t *caps = mutable_bits(dev, type, &maxcode);
        if (!caps || code > maxcode || !bit_test(caps, code)) return;
        if (type == NAS_INPUT_EV_KEY) {
            if (value != 0 && value != 1 && value != 2) return;
            bool pressed = bit_test(dev->key_state, code);
            if (value == 2) {
                if (!pressed) return;
            } else {
                if (pressed == (value != 0)) return;
                if (value) bit_set(dev->key_state, code);
                else bit_clear(dev->key_state, code);
            }
        } else if (type == NAS_INPUT_EV_ABS) {
            if (dev->absinfo[code].value == value) return;
            dev->absinfo[code].value = value;
        }
    }
    uint64_t ns = nas_arch_monotonic_ns();
    struct nas_input_event event = {
        .tv_sec = (int64_t)(ns / UINT64_C(1000000000)),
        .tv_usec = (int64_t)((ns % UINT64_C(1000000000)) / 1000U),
        .type = type, .code = code, .value = value
    };
    nas_evdev_publish(dev, &event);
    /* KEY_A (Linux code 30) identifies a keyboard; mouse/touchpad buttons
     * are EV_KEY as well and must not leak into the foreground TTY. The
     * event is dispatched to both independent consumers just as Linux's
     * input handler fan-out does, without converting it into serial bytes. */
    if (type == NAS_INPUT_EV_KEY && keyboard_handler &&
        bit_test(dev->keybit,30U))
        keyboard_handler(code,value!=0);
}
