/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NASEED_INPUT_CORE_H
#define NASEED_INPUT_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Linux input.h UAPI event type/code limits (6.x), common to LP64 ISAs.
 * Device drivers register capabilities; handlers consume hardware-independent
 * events. The driver never owns an evdev File, TTY or compositor state. */
#define NAS_INPUT_MAX_DEVICES 16U
#define NAS_INPUT_NAME_BYTES 80U
#define NAS_INPUT_EV_SYN 0U
#define NAS_INPUT_EV_KEY 1U
#define NAS_INPUT_EV_REL 2U
#define NAS_INPUT_EV_ABS 3U
#define NAS_INPUT_EV_MSC 4U
#define NAS_INPUT_EV_SW 5U
#define NAS_INPUT_EV_LED 17U
#define NAS_INPUT_EV_SND 18U
#define NAS_INPUT_EV_REP 20U
#define NAS_INPUT_SYN_REPORT 0U
#define NAS_INPUT_SYN_DROPPED 3U
#define NAS_INPUT_KEY_MAX 0x2ffU
#define NAS_INPUT_REL_MAX 0x0fU
#define NAS_INPUT_ABS_MAX 0x3fU
#define NAS_INPUT_PROP_MAX 0x1fU
#define NAS_INPUT_BIT_BYTES(maxcode) (((maxcode) + 8U) / 8U)

struct nas_input_id {
    uint16_t bustype, vendor, product, version;
};
struct nas_input_absinfo {
    int32_t value, minimum, maximum, fuzz, flat, resolution;
};
/* Exact Linux LP64 struct input_event wire ABI: timeval + type/code/value. */
struct nas_input_event {
    int64_t tv_sec, tv_usec;
    uint16_t type, code;
    int32_t value;
};
_Static_assert(sizeof(struct nas_input_event) == 24,
               "LP64 Linux input_event ABI must be 24 bytes");

struct nas_input_device {
    char name[NAS_INPUT_NAME_BYTES];
    struct nas_input_id id;
    uint32_t evbit;
    uint8_t keybit[NAS_INPUT_BIT_BYTES(NAS_INPUT_KEY_MAX)];
    uint8_t relbit[NAS_INPUT_BIT_BYTES(NAS_INPUT_REL_MAX)];
    uint8_t absbit[NAS_INPUT_BIT_BYTES(NAS_INPUT_ABS_MAX)];
    uint8_t mscbit[1];
    uint8_t swbit[3];
    uint8_t ledbit[2];
    uint8_t sndbit[1];
    uint8_t repbit[1];
    uint8_t propbit[NAS_INPUT_BIT_BYTES(NAS_INPUT_PROP_MAX)];
    struct nas_input_absinfo absinfo[NAS_INPUT_ABS_MAX + 1U];
    uint8_t key_state[NAS_INPUT_BIT_BYTES(NAS_INPUT_KEY_MAX)];
    unsigned index;             /* stable eventN ordinal while registered */
    unsigned readers;           /* pinned independent OpenFile consumers */
    bool registered;
};

void nas_input_device_init(struct nas_input_device *, const char *name);
int nas_input_set_capability(struct nas_input_device *, uint16_t type,
                             uint16_t code);
int nas_input_set_property(struct nas_input_device *, uint16_t code);
int nas_input_set_absinfo(struct nas_input_device *, uint16_t code,
                          int32_t min, int32_t max, int32_t fuzz,
                          int32_t flat, int32_t resolution);
int nas_input_register_device(struct nas_input_device *);
/* May not unbind while evdev has open descriptions: returns -EBUSY. */
int nas_input_unregister_device(struct nas_input_device *);
const struct nas_input_device *nas_input_device_at(unsigned index);
/* Capability blob for Linux EVIOCGBIT; size in bytes, NULL if unsupported. */
const uint8_t *nas_input_capabilities(const struct nas_input_device *,
                                      uint16_t type, size_t *bytes);
/* The emitting driver is responsible for pairing events with SYN_REPORT.
 * Dispatch in a serialized kernel context, never switch Tasks inside IRQ. */
void nas_input_event(struct nas_input_device *, uint16_t type,
                     uint16_t code, int32_t value);

#endif
