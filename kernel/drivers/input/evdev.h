/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NASEED_INPUT_EVDEV_H
#define NASEED_INPUT_EVDEV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "core.h"

/* Linux input event character-device handler. One independently allocated
 * client belongs to one shared OpenFile description, NOT to one FD number.
 * dup/fork inherit its event position; a second open gets its own stream. */
struct nas_evdev_client;
typedef void (*nas_evdev_wake_fn)(void *opaque);

int nas_evdev_open(unsigned device_index, struct nas_evdev_client **out);
void nas_evdev_close(struct nas_evdev_client *);
/* Read complete 24-byte records into trusted kernel storage, never user VA.
 * Empty: -EAGAIN; insufficient nonzero count: -EINVAL; zero count: 0.
 * File ops provide blocking/O_NONBLOCK semantics around this primitive. */
int nas_evdev_read(struct nas_evdev_client *, void *dst, size_t bytes);
bool nas_evdev_ready(const struct nas_evdev_client *);
const struct nas_input_device *nas_evdev_device(const struct nas_evdev_client *);
void nas_evdev_set_wake(struct nas_evdev_client *, nas_evdev_wake_fn,
                        void *opaque);

/* Input-core -> evdev handler entry, deliberately not a transport callback.
 * Driver may only report through nas_input_event(). */
void nas_evdev_publish(const struct nas_input_device *,
                       const struct nas_input_event *);

#endif
