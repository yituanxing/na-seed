#ifndef NASEED_LINUX_VIRTGPU_VIRTIO_PORT_H
#define NASEED_LINUX_VIRTGPU_VIRTIO_PORT_H
/* Linux v6.14 virtqueue API surface adapted to NaSeed's real split ring.
 * Supports pre-mapped, pinned SG only; NOT Linux's general DMA API or
 * indirect descriptors. vdev intentionally stays opaque here because
 * drm_device/virtio_device registration is a separate porting stage. */
#include <linux/scatterlist.h>
#include <stdbool.h>
#include <stdint.h>
typedef unsigned int gfp_t;
struct virtio_device;
struct virtqueue {
    void (*callback)(struct virtqueue *);
    const char *name;
    struct virtio_device *vdev;
    unsigned int index;
    unsigned int num_free;
    unsigned int num_max;
    bool reset;
    void *priv;
};
/* The exact upstream Linux virtio.h function signatures used by
 * unmodified virtgpu_vq.c; no synthetic GPU commands are implemented. */
int virtqueue_add_sgs(struct virtqueue *,struct scatterlist *sgs[],
                      unsigned int out_sgs,unsigned int in_sgs,
                      void *data,gfp_t gfp);
void *virtqueue_get_buf(struct virtqueue *,unsigned int *len);
bool virtqueue_kick_prepare(struct virtqueue *);
bool virtqueue_notify(struct virtqueue *);
void virtqueue_disable_cb(struct virtqueue *);
bool virtqueue_enable_cb(struct virtqueue *);
#endif
