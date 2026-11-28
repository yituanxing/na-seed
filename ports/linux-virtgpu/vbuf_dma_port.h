#ifndef NASEED_LINUX_VIRTGPU_VBUF_DMA_PORT_H
#define NASEED_LINUX_VIRTGPU_VBUF_DMA_PORT_H
/* Small Linux vbuf command/response bounce buffers backed by the SAME
 * NaSeed VirtIO DMA provider used by the real split ring. No GPU commands.
 *
 * Original Linux kmalloc/slab memory is NOT assumed DMA-able. Prepare copies
 * an outgoing command into a provider-owned coherent DMA page; an incoming
 * response is copied back only after the actual USED completion. This is
 * currently limited to one page and the QEMU provider with frame mapping.
 * Larger vmalloc/sg_table and shmem resource backing remain separate work. */
#include "../../kernel/drivers/port/dma.h"
#include "virtqueue_api_port.h"
#include <stdint.h>
#include <stdbool.h>
struct nas_linux_virtgpu_vbuf_dma {
    const struct nas_dma_provider *provider;
    struct nas_dma_page page;
    struct nas_dma_mapped_frame pin;
    void *original;
    uint32_t bytes;
    bool device_writes;
};
/* Buffer must be zero-initialized. On a cleanup failure, ownership of
 * page/pin is retained: use retire AFTER device quiescence to retry. */
int nas_linux_virtgpu_vbuf_dma_prepare(struct nas_linux_virtgpu_vbuf_dma *,
                                      const struct nas_dma_provider *,
                                      void *original,uint32_t bytes,
                                      bool device_writes);
/* Register the mapped DMA page for a Linux-v6.14-layout SG. The caller
 * must preserve the vbuf DMA page and its SG registration until
 * virtqueue_add_sgs; its DMA page stays alive until USED or reset. */
int nas_linux_virtgpu_vbuf_dma_register_sg(
    struct nas_linux_virtgpu_virtqueue_port *,
    struct scatterlist *,
    const struct nas_linux_virtgpu_vbuf_dma *);
/* Caller has verified an actual used-ring completion (completed=true) or
 * quiescence after reset (completed=false). When completed, copy response
 * AFTER DMA sync. Never copy an unverified response after reset.
 * This function does not itself wait for GPU/device quiescence. */
int nas_linux_virtgpu_vbuf_dma_retire_after_quiesce(
    struct nas_linux_virtgpu_vbuf_dma *,bool completed);
#endif
