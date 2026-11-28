#include "virtqueue_api_port.h"
#include "../../kernel/errno.h"
#include <stddef.h>
#include <stdint.h>

/* A real DMA pin is registered per SG pointer in the NaSeed queue owner.
 * Never extend/modify Linux's struct scatterlist ABI with private fields.
 * Registrations are scoped to one subsequent virtqueue_add_sgs() call. */
int nas_linux_virtgpu_sg_map(struct nas_linux_virtgpu_virtqueue_port *port,
                            struct scatterlist *sg,
                            const struct nas_dma_mapped_frame *map,
                            uint32_t offset,uint32_t length) {
    if (!port || !port->bridge.queue || !sg ||
        (sg->page_link & SG_CHAIN) ||
        !map || !map->active || !map->usable ||
        map->provider!=port->bridge.queue->transport->dma)return -EINVAL;
    if(port->sg_count>=NAS_VRING_MAX_DESC)return -ENOSPC;
    for(unsigned i=0;i<port->sg_count;i++)
        if(port->sg_maps[i].sg==sg)return -EBUSY;
    uint64_t bus=0;
    int rc=nas_dma_mapped_slice(map,offset,length,&bus);
    if(rc)return rc;
    sg->offset=offset;
    sg->length=length;
    sg->dma_address=bus;
    port->sg_maps[port->sg_count++]=(struct nas_linux_virtgpu_sg_registration){
        .sg=sg,.map=map};
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
    port->sg_count=0;
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
    (void)gfp; /* Submission itself makes no allocations. */
    struct nas_linux_virtgpu_virtqueue_port *port=get_port(vq);
    if(!port)return -EINVAL;
    struct nas_vring_queue *queue=port->bridge.queue;
    int rc=-EINVAL;
    if(!sgs || !data || !out_sgs || out_sgs>NAS_VRING_MAX_DESC ||
       in_sgs>NAS_VRING_MAX_DESC-out_sgs)goto done;
    if(!queue->active || queue->poisoned){rc=-EIO;goto done;}
    struct nas_linux_virtgpu_dma_sg mapped[NAS_VRING_MAX_DESC];
    unsigned n=0;
    const unsigned list_count=out_sgs+in_sgs;
    /* Each sg list may include MANY descriptors. Traverse canonical Linux
     * SG_END/SG_CHAIN links and validate every registered DMA page owner
     * before publishing a single device-visible descriptor. */
    for(unsigned i=0;i<list_count;i++) {
        struct scatterlist *sg=sgs[i];
        if(!sg)goto done;
        while(sg) {
            if(n>=NAS_VRING_MAX_DESC){rc=-ENOSPC;goto done;}
            if(sg->page_link & SG_CHAIN)goto done;
            const struct nas_dma_mapped_frame *map=NULL;
            for(unsigned j=0;j<port->sg_count;j++)
                if(port->sg_maps[j].sg==sg) {
                    map=port->sg_maps[j].map;
                    break;
                }
            if(!map || !map->active || !map->usable ||
               map->provider!=queue->transport->dma)goto done;
            uint64_t bus=0;
            if(nas_dma_mapped_slice(map,sg->offset,sg->length,&bus) ||
               sg->dma_address!=bus)goto done;
            mapped[n++]=(struct nas_linux_virtgpu_dma_sg){
                .map=map,.offset=sg->offset,.length=sg->length,
                .device_writes=(i>=out_sgs)};
            sg=sg_next(sg);
        }
    }
    if(n>queue->free_count){rc=-ENOSPC;goto done;}
    rc=nas_linux_virtgpu_vq_submit_mapped(&port->bridge,mapped,n,data);
done:
    /* The next submission must REGISTER its own DMA ownership, so a
     * stale stack-allocated Linux SG can never inherit an earlier mapping.
     * A failed attempt must re-register before retrying. */
    port->sg_count=0;
    /* Even a failed post-publication DMA sync may consume descriptors. */
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
