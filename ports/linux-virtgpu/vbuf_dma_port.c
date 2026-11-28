#include "vbuf_dma_port.h"
#include "../../kernel/errno.h"
#include "../../kernel/lib/string.h"
#include <stdint.h>
#include <stddef.h>

/* Genuine upstream VirtIO-GPU command/response payloads are small; this
 * primitive maps a single page, not arbitrary Linux kmalloc memory. */
int nas_linux_virtgpu_vbuf_dma_prepare(struct nas_linux_virtgpu_vbuf_dma *b,
                                      const struct nas_dma_provider *provider,
                                      void *original,uint32_t bytes,
                                      bool device_writes) {
    if(!b || !provider || !original || !bytes ||
       bytes>NAS_DMA_PAGE_BYTES)return -EINVAL;
    if(b->provider || b->page.cpu || b->pin.active)return -EBUSY;
    /* Do not claim noncoherent hardware can safely use this QEMU-only path.
     * A provider mapping is necessary even if the CPU buffer came from heap. */
    if(!provider->coherent || !provider->map_frame ||
       !provider->unmap_frame)return -EOPNOTSUPP;
    b->provider=provider;
    b->original=original;
    b->bytes=bytes;
    b->device_writes=device_writes;
    int rc=nas_dma_alloc_page(provider,&b->page);
    if(rc)goto failure;
    if(!b->page.frame_pa){rc=-EOPNOTSUPP;goto failure;}
    rc=nas_dma_map_borrowed_frame(provider,b->page.frame_pa,&b->pin);
    if(rc)goto failure;
    if(device_writes)
        memset(b->page.cpu,0,bytes);
    else
        memcpy(b->page.cpu,original,bytes);
    rc=nas_dma_sync(provider,&b->page,0,bytes,
                    device_writes?NAS_DMA_FROM_DEVICE:NAS_DMA_TO_DEVICE);
    if(!rc)return 0;
failure:
    /* Nothing has been published yet, so local unwind is safe here.
     * If an unmap/release fails, preserve the owner for explicit retry. */
    if(b->pin.active) {
        int clean=nas_dma_unmap_borrowed_frame(&b->pin);
        if(clean)return clean;
    }
    if(b->page.cpu) {
        int clean=nas_dma_free_page(provider,&b->page);
        if(clean)return clean;
    }
    *b=(struct nas_linux_virtgpu_vbuf_dma){0};
    return rc;
}
int nas_linux_virtgpu_vbuf_dma_register_sg(
    struct nas_linux_virtgpu_virtqueue_port *port,
    struct scatterlist *sg,const struct nas_linux_virtgpu_vbuf_dma *b) {
    if(!port || !sg || !b || !b->page.cpu || !b->pin.active ||
       !b->pin.usable || !b->provider ||
       !b->bytes || b->bytes>NAS_DMA_PAGE_BYTES)return -EINVAL;
    return nas_linux_virtgpu_sg_map(port,sg,&b->pin,0,b->bytes);
}
int nas_linux_virtgpu_vbuf_dma_retire_after_quiesce(
    struct nas_linux_virtgpu_vbuf_dma *b,bool completed) {
    if(!b || !b->provider || !b->page.cpu)return -EINVAL;
    if(completed && b->device_writes && b->pin.active) {
        int rc=nas_dma_sync(b->provider,&b->page,0,b->bytes,
                            NAS_DMA_FROM_DEVICE);
        if(rc)return rc; /* Don't copy incomplete/unsynced response. */
        memcpy(b->original,b->page.cpu,b->bytes);
    }
    if(b->pin.active) {
        int rc=nas_dma_unmap_borrowed_frame(&b->pin);
        if(rc)return rc;
    }
    int rc=nas_dma_free_page(b->provider,&b->page);
    if(rc)return rc; /* Retain page ownership after failed release. */
    *b=(struct nas_linux_virtgpu_vbuf_dma){0};
    return 0;
}
