#ifndef NASEED_LINUX_VIRTGPU_VQ_PAIR_PORT_H
#define NASEED_LINUX_VIRTGPU_VQ_PAIR_PORT_H
/* Genuine TWO-queue VirtIO GPU layout (control=0, cursor=1) over the
 * shared NaSeed PCI/MMIO transport. Kept separate from the active 2D class
 * driver until the original Linux DRM stack is ready to claim the device.
 *
 * IRQ top-half ONLY records the VirtIO interrupt source; Linux callbacks
 * are invoked in a deferred process-context service and may schedule
 * upstream dequeue_work. No response is consumed or fence signalled here.
 * The caller provides serialization and drains work BEFORE device detach.
 */
#include "virtqueue_api_port.h"
struct nas_linux_virtgpu_vq_pair {
    const struct nas_virtio_transport *transport;
    struct nas_vring_queue rings[2];
    struct nas_linux_virtgpu_virtqueue_port vqs[2];
    uint32_t irq_pending;
    bool initialized;
};
/* Transport must already have completed feature negotiation. Caller owns
 * the transport and DMA provider for the full lifetime of both queues.
 * No indirect descriptor feature is advertised by this adapter. */
int nas_linux_virtgpu_vq_pair_init(
    struct nas_linux_virtgpu_vq_pair *,
    const struct nas_virtio_transport *,
    struct virtio_device *,
    unsigned descriptors_per_queue,
    void (*ctrl_cb)(struct virtqueue *),
    void (*cursor_cb)(struct virtqueue *));
/* After both device queues are allocated and fully initialized. */
int nas_linux_virtgpu_vq_pair_activate(struct nas_linux_virtgpu_vq_pair *);
/* Read/ack the real PCI or MMIO VirtIO device IRQ source. The top
 * half records only bits; the GPU callbacks run later in process context.
 * Returns zero if the interrupt belonged to another shared-line device. */
uint32_t nas_linux_virtgpu_vq_pair_irq_ack(struct nas_linux_virtgpu_vq_pair *);
/* For already acknowledged sources, record only the verified device bits. */
void nas_linux_virtgpu_vq_pair_irq_record(struct nas_linux_virtgpu_vq_pair *,
                                         uint32_t asserted);
/* Invoke the Linux callbacks in process context if a used-ring actually
 * changed. Callbacks may schedule upstream dequeue_work but MUST NOT be
 * faked as having consumed responses or signalled fences. */
int nas_linux_virtgpu_vq_pair_service(struct nas_linux_virtgpu_vq_pair *);
/* Caller MUST have quiesced IRQ/work and retained all outstanding command
 * cookies. Transport reset happens before freeing either ring DMA page;
 * abandoned cookies remain available via each vqs[i].bridge after close.
 * A close failing DMA release must be retried, never overwrite the object. */
int nas_linux_virtgpu_vq_pair_close(struct nas_linux_virtgpu_vq_pair *);
#endif
