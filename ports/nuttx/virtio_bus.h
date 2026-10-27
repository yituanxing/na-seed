#ifndef NASEED_NUTTX_VIRTIO_BUS_H
#define NASEED_NUTTX_VIRTIO_BUS_H
/* Transport-neutral bus binding for NuttX-derived VirtIO-Blk. */
unsigned nas_nuttx_virtio_blk_count(void);
int nas_nuttx_virtio_blk_driver_register(void);
#endif
