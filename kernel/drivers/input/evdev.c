/* SPDX-License-Identifier: GPL-2.0-only */
/* File-client/packet semantics ported from Linux drivers/input/evdev.c;
 * Linux's per-open-description ring contract is preserved here. */
#include "evdev.h"
#include "../../errno.h"
#include "../../lib/string.h"
#include "../../mm/heap.h"

/* Linux evdev's packet_head boundary is essential: consumers must not read
 * a partial EV_SYN-framed device update. A full client ring discards stale
 * state and announces SYN_DROPPED on the next SYN_REPORT (per Linux ABI).
 * There is no global consumptive queue: independent open descriptions each
 * receive every event, and dup/fork share precisely one such description. */
#define NAS_EVDEV_RING_SIZE 256U
_Static_assert((NAS_EVDEV_RING_SIZE & (NAS_EVDEV_RING_SIZE - 1U)) == 0,
               "evdev ring size must be power of two");
struct nas_evdev_client {
    struct nas_evdev_client *next;
    struct nas_input_device *device;
    unsigned head, tail, packet_head;
    bool dropped;
    nas_evdev_wake_fn wake;
    void *wake_context;
    struct nas_input_event buffer[NAS_EVDEV_RING_SIZE];
};
static struct nas_evdev_client *clients;

static unsigned next_position(unsigned offset) {
    return (offset + 1U) & (NAS_EVDEV_RING_SIZE - 1U);
}

int nas_evdev_open(unsigned index, struct nas_evdev_client **out) {
    if (!out) return -EINVAL;
    *out = NULL;
    /* Registration is process-context only, serialized with open/close.
     * The device owner cannot unregister until this reader is closed. */
    struct nas_input_device *dev = (struct nas_input_device *)
                                   nas_input_device_at(index);
    if (!dev || !dev->registered) return -ENODEV;
    if (dev->readers == UINT32_MAX) return -ENOSPC;
    struct nas_evdev_client *client = nas_heap_calloc(1U, sizeof(*client));
    if (!client) return -ENOMEM;
    client->device = dev;
    client->next = clients;
    clients = client;
    dev->readers++;
    *out = client;
    return 0;
}

void nas_evdev_close(struct nas_evdev_client *client) {
    if (!client) return;
    struct nas_evdev_client **link = &clients;
    while (*link && *link != client) link = &(*link)->next;
    if (*link != client) return;
    *link = client->next;
    if (client->device->readers) client->device->readers--;
    (void)nas_heap_free(client);
}

bool nas_evdev_ready(const struct nas_evdev_client *client) {
    return client && client->tail != client->packet_head;
}

const struct nas_input_device *nas_evdev_device(
        const struct nas_evdev_client *client) {
    return client ? client->device : NULL;
}

void nas_evdev_set_wake(struct nas_evdev_client *client,
                        nas_evdev_wake_fn callback, void *opaque) {
    if (!client) return;
    client->wake = callback;
    client->wake_context = opaque;
}

int nas_evdev_read(struct nas_evdev_client *client, void *dst, size_t bytes) {
    if (!client || (!dst && bytes)) return -EINVAL;
    if (!bytes) return 0;
    if (bytes < sizeof(struct nas_input_event)) return -EINVAL;
    if (!nas_evdev_ready(client)) return -EAGAIN;
    size_t copied = 0;
    uint8_t *target = dst;
    while (bytes - copied >= sizeof(struct nas_input_event) &&
           client->tail != client->packet_head) {
        memcpy(target + copied, &client->buffer[client->tail],
               sizeof(struct nas_input_event));
        client->tail = next_position(client->tail);
        copied += sizeof(struct nas_input_event);
    }
    return (int)copied;
}

static void enqueue(struct nas_evdev_client *client,
                    const struct nas_input_event *event) {
    client->buffer[client->head] = *event;
    client->head = next_position(client->head);
}

void nas_evdev_publish(const struct nas_input_device *device,
                       const struct nas_input_event *event) {
    if (!device || !event) return;
    for (struct nas_evdev_client *client = clients; client;
         client = client->next) {
        if (client->device != device) continue;
        const bool report = event->type == NAS_INPUT_EV_SYN &&
                            event->code == NAS_INPUT_SYN_REPORT;
        if (client->dropped && !report) continue;
        if (!client->dropped && next_position(client->head) == client->tail) {
            /* Complete loss of packet coherence. Reset only this reader,
             * then publish a SYN_DROPPED followed by the next frame edge. */
            client->head = client->tail = client->packet_head = 0;
            struct nas_input_event lost = *event;
            lost.type = NAS_INPUT_EV_SYN;
            lost.code = NAS_INPUT_SYN_DROPPED;
            lost.value = 0;
            enqueue(client, &lost);
            client->dropped = true;
        }
        if (!client->dropped || report) enqueue(client, event);
        if (report) {
            client->dropped = false;
            client->packet_head = client->head;
            if (client->wake) client->wake(client->wake_context);
        }
    }
}
