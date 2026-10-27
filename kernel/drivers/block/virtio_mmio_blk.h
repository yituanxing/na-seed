#ifndef NASEED_VIRTIO_BLK_FALLBACK_H
#define NASEED_VIRTIO_BLK_FALLBACK_H
#include "../virtio/transport.h"
/* Temporary, transport-independent VirtIO-Blk block-request backend.
 * The platform discovers devices, bus performs driver match/probe.
 */
int nas_virtio_blk_attach(struct nas_virtio_transport *);
unsigned nas_virtio_blk_count(void);
int nas_virtio_blk_driver_register(void);
#endif
