#include "virtqueue_api_port.h"
#include "../../kernel/errno.h"
#include <stddef.h>
#include <stdint.h>

/* Linux sg_init_one() does not establish a DMA mapping by itself. An
 * allocator/provider must first pin the real NaSeed physical page and
 * prepare this SG with its mapped device address. Never virt_to_phys()
 * arbitrary DRM heap memory or guess identity DMA here. */
int nas_linux_virtgpu_sg_map(struct scatterlist *sg,
                            const struct nas_dma_mapped_frame *map,
                            uint32_t offset,uint32_t length) {
    if (!sg || (sg->page_link & SG_CHAIN))return -EINVAL;
    uint64_t bus=0;
    int rc=nas_dma_mapped_slice(map,offset,length,&bus);
    if(rc)return rc;
    sg->offset=offset;
    sg->length=length;
    sg->dma_address=bus;
    sg->nas_dma_owner=map;
    return 0;
}
int nas_linux_virtgpu_virtqueue_bind(
    struct nas_linux_virtgpu_virtqueue_port *port,
    struct nas_vring_queue *queue,struct virtio_device *vdev,
    unsigned index) {
    if (!port || !queue || index>UINT16_MAX)return -EINVAL;
    int rc=nas_linux_virtgpu_vq_bind(&port->bridge,queue);
    if(rc)return rc;
    port->linux_vq=(struct virtqueue){
        .vdev=vdev,.index=index,.num_free=queue->free_count,
        .num_max=queue->ring.num,.priv=port};
    return 0;
}
static struct nas_linux_virtgpu_virtqueue_port *get_port(
    struct virtqueue *vq) {
    if(!vq || !vq->priv)return NULL;
    struct nas_linux_virtgpu_virtqueue_port *port=vq->priv;
    if(&port->linux_vq!=vq || !port->bridge.queue)return NULL;
    return port;
}
int virtqueue_add_sgs(struct virtqueue *vq,struct scatterlist *sgs[],
                      unsigned int out_sgs,unsigned int in_sgs,
                      void *data,gfp_t gfp) {
    (void)gfp; /* No allocation in this split-ring submission. */
    struct nas_linux_virtgpu_virtqueue_port *port=get_port(vq);
    if(!port || !sgs || !data || !out_sgs || out_sgs>NAS_VRING_MAX_DESC ||
       in_sgs>NAS_VRING_MAX_DESC-out_sgs)return -EINVAL;
    struct nas_vring_queue *queue=port->bridge.queue;
    if(!queue->active || queue->poisoned)return -EIO;
    struct nas_linux_virtgpu_dma_sg mapped[NAS_VRING_MAX_DESC];
    unsigned n=0;
    const unsigned list_count=out_sgs+in_sgs;
    /* Linux's list count is NOT its descriptor count: an SG list can
     * contain many entries. The Linux SG_END and SG_CHAIN markers govern
     * sg_next(); a full chain must fit in the negotiated direct ring.
     * Indirect descriptors are deliberately not advertised or fabricated. */
    for(unsigned i=0;i<list_count;i++) {
        struct scatterlist *sg=sgs[i];
        if(!sg)return -EINVAL;
        while(sg) {
            if(n>=NAS_VRING_MAX_DESC)return -ENOSPC;
            if((sg->page_link & SG_CHAIN) || !sg->nas_dma_owner ||
               !sg->nas_dma_owner->active || !sg->nas_dma_owner->usable ||
               sg->nas_dma_owner->provider!=queue->transport->dma)
                return -EINVAL;
            uint64_t expected_bus=0;
            if(nas_dma_mapped_slice(sg->nas_dma_owner,sg->offset,
                                    sg->length,&expected_bus) ||
               sg->dma_address!=expected_bus)return -EINVAL;
            mapped[n++]=(struct nas_linux_virtgpu_dma_sg){
                .map=sg->nas_dma_owner,.offset=sg->offset,
                .length=sg->length,.device_writes=(i>=out_sgs)};
            sg=sg_next(sg);
        }
    }
    if(n>queue->free_count) {
        vq->num_free=queue->free_count;
        return -ENOSPC;
    }
    int rc=nas_linux_virtgpu_vq_submit_mapped(&port->bridge,mapped,n,data);
    /* Even a post-publication DMA-sync failure can consume descriptors.
     * Linux's num_free must track the REAL queue state on every return. */
    vq->num_free=queue->free_count;
    return rc;
}
void *virtqueue_get_buf(struct virtqueue *vq,unsigned int *len) {
    struct nas_linux_virtgpu_virtqueue_port *port=get_port(vq);
    if(!port || !len)return NULL;
    void *cookie=NULL;
    uint32_t used=0;
    int rc=nas_linux_virtgpu_vq_reclaim(&port->bridge,&cookie,&used);
    vq->num_free=port->bridge.queue->free_count;
    if(rc)return NULL; /* -EAGAIN or poisoned device: never fake success */
    *len=used;
    return cookie;
}
bool virtqueue_kick_prepare(struct virtqueue *vq) {
    struct nas_linux_virtgpu_virtqueue_port *port=get_port(vq);
    return port && nas_linux_virtgpu_vq_kick_prepare(&port->bridge);
}
bool virtqueue_notify(struct virtqueue *vq) {
    struct nas_linux_virtgpu_virtqueue_port *port=get_port(vq);
    if(!port || !port->bridge.queue->active || port->bridge.queue->poisoned)
        return false;
    nas_linux_virtgpu_vq_notify(&port->bridge);
    return true;
}
void virtqueue_disable_cb(struct virtqueue *vq) {
    struct nas_linux_virtgpu_virtqueue_port *port=get_port(vq);
    if(port)nas_linux_virtgpu_vq_disable_callbacks(&port->bridge);
}
bool virtqueue_enable_cb(struct virtqueue *vq) {
    struct nas_linux_virtgpu_virtqueue_port *port=get_port(vq);
    return port && nas_linux_virtgpu_vq_enable_callbacks(&port->bridge);
}
