/* VirtIO v1.2 block request engine -> NaSeed BlockDevice.
 * One synchronous polled split queue is retained as the proven fallback.
 * Transport operations, MMIO register layout, DMA and bus discovery are
 * independent so a pinned mature third-party driver can replace this
 * backend without changing VFS, filesystem or PCI/MMIO platform code.
 */
#include "virtio_mmio_blk.h"
#include "../port/block.h"
#include "../port/dma.h"
#include "../core/bus.h"
#include "../virtio/transport.h"
#include <virtio/virtio_ring.h>
#include "../../mm/frame.h"
#include "../../errno.h"
#include "../../lib/string.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_DISKS 4U
#define QUEUE_SIZE 8U
#define MAX_TRANSFER 4096U
#define DEVICE_BLK 2U
#define FEAT_RO    (UINT64_C(1)<<5)
#define FEAT_FLUSH (UINT64_C(1)<<9)
#define REQ_IN         0U
#define REQ_OUT        1U
#define REQ_FLUSH      4U
struct virtio_header {uint32_t type,reserved;uint64_t sector;};
struct virt_blk {
    struct nas_port_block port;
    struct vring vr;
    struct nas_virtio_transport *transport;
    const struct nas_dma_provider *dma;
    struct nas_dma_page queue,request,payload;
    uint16_t used_index;
    bool failed;
};
static struct virt_blk disks[MAX_DISKS];
static const char *const names[MAX_DISKS]={
    "virtio0","virtio1","virtio2","virtio3"
};
static unsigned disk_count;
static void barrier(void) {
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}
static void release_pages(struct virt_blk *d) {
    if (d->payload.cpu) (void)nas_dma_free_page(d->dma,&d->payload);
    if (d->request.cpu) (void)nas_dma_free_page(d->dma,&d->request);
    if (d->queue.cpu) (void)nas_dma_free_page(d->dma,&d->queue);
}
static int virtio_submit(struct virt_blk *d,uint32_t type,
                         uint64_t sector,void *data,uint32_t bytes) {
    if(!d || d->failed || !d->queue.cpu || bytes>MAX_TRANSFER)
        return -EIO;
    if((type==REQ_FLUSH && bytes) ||
       (type!=REQ_FLUSH && (!bytes || !data)))return -EINVAL;
    /* Actual upstream Unikraft/Linux BSD virtqueue layout; no NaSeed-only
     * duplicated ring structs or magic avail/used address offsets. */
    struct vring_desc *desc=d->vr.desc;
    volatile struct vring_avail *avail=d->vr.avail;
    volatile struct vring_used *used=d->vr.used;
    struct virtio_header *hdr=(struct virtio_header *)d->request.cpu;
    volatile uint8_t *status=(volatile uint8_t *)
                              (uint8_t *)d->request.cpu+0x20U;
    hdr->type=type;hdr->reserved=0;hdr->sector=sector;
    *status=0xffU;
    if(type==REQ_OUT)memcpy(d->payload.cpu,data,bytes);
    desc[0]=(struct vring_desc){
        .addr=d->request.bus_address,.len=sizeof(*hdr),
        .flags=VRING_DESC_F_NEXT,.next=type==REQ_FLUSH?2U:1U
    };
    if(type!=REQ_FLUSH)
        desc[1]=(struct vring_desc){
            .addr=d->payload.bus_address,.len=bytes,
            .flags=VRING_DESC_F_NEXT|(type==REQ_IN?VRING_DESC_F_WRITE:0U),.next=2U
        };
    desc[2]=(struct vring_desc){
        .addr=d->request.bus_address+0x20U,.len=1,.flags=VRING_DESC_F_WRITE
    };
    /* This polled fallback needs coherent memory; future IRQ/uncached
     * backends can implement a noncoherent completion protocol independently. */
    if(!d->dma->coherent)return -EOPNOTSUPP;
    /* A single inflight chain is serialized on the kernel CPU. All split
     * queue entries and DMA frames are physical and have stable lifetimes. */
    /* DMA request header and outgoing payload precede queue publication. */
    if(nas_dma_sync(d->dma,&d->request,0,64U,NAS_DMA_TO_DEVICE) ||
       (bytes && type==REQ_OUT &&
        nas_dma_sync(d->dma,&d->payload,0,bytes,NAS_DMA_TO_DEVICE))) return -EIO;
    uint16_t submit_index=avail->idx;
    avail->ring[submit_index%QUEUE_SIZE]=0;
    barrier();
    avail->idx=(uint16_t)(submit_index+1);
    if(nas_dma_sync(d->dma,&d->queue,0,NAS_DMA_PAGE_BYTES,
                    NAS_DMA_TO_DEVICE)) return -EIO;
    barrier();
    nas_virtio_notify(d->transport,0);
    bool completed=false;
    for(unsigned spin=0;spin<30000000U;spin++) {
        barrier();
        if(used->idx!=(uint16_t)d->used_index){
            completed=true;break;
        }
    }
    if(!completed){
        d->failed=true;
        nas_virtio_fail(d->transport);
        return -EIO;
    }
    barrier();
    if(nas_dma_sync(d->dma,&d->queue,0,4096U,NAS_DMA_FROM_DEVICE) ||
       nas_dma_sync(d->dma,&d->request,0,64U,NAS_DMA_FROM_DEVICE) ||
       (bytes && type==REQ_IN &&
        nas_dma_sync(d->dma,&d->payload,0,bytes,NAS_DMA_FROM_DEVICE))) return -EIO;
    unsigned item=d->used_index%QUEUE_SIZE;
    uint32_t id=used->ring[item].id;
    d->used_index++;
    if(id!=0 || *status!=0)return *status==2?-EOPNOTSUPP:-EIO;
    if(type==REQ_IN)memcpy(data,d->payload.cpu,bytes);
    return 0;
}
static int64_t blk_read(void *backend,uint64_t sector,
                         uint32_t count,void *dst){
    struct virt_blk *d=(struct virt_blk *)backend;
    if(count>MAX_TRANSFER/512U)return -EINVAL;
    return virtio_submit(d,REQ_IN,sector,dst,count*512U);
}
static int64_t blk_write(void *backend,uint64_t sector,
                          uint32_t count,const void *src){
    struct virt_blk *d=(struct virt_blk *)backend;
    if(count>MAX_TRANSFER/512U)return -EINVAL;
    return virtio_submit(d,REQ_OUT,sector,(void *)src,count*512U);
}
static int blk_flush(void *backend){
    return virtio_submit((struct virt_blk *)backend,REQ_FLUSH,0,0,0);
}
static const struct nas_port_block_ops disk_ops={
    .read=blk_read,.write=blk_write,.flush=blk_flush
};
static int configure(struct virt_blk *d) {
    uint64_t negotiated=0;
    int rc=nas_virtio_negotiate(d->transport,0,FEAT_FLUSH,&negotiated);
    if(rc)return rc;
    d->dma=d->transport->dma;
    if(vring_size(QUEUE_SIZE,256U)>NAS_DMA_PAGE_BYTES)return -EOVERFLOW;
    if(nas_dma_alloc_page(d->dma,&d->queue) ||
       nas_dma_alloc_page(d->dma,&d->request) ||
       nas_dma_alloc_page(d->dma,&d->payload))return -ENOMEM;
    /* Preserve byte-identical upstream Unikraft split ring layout. */
    void *queue_va=d->queue.cpu;
    memset(queue_va,0,NAS_DMA_PAGE_BYTES);
    vring_init(&d->vr,QUEUE_SIZE,queue_va,256U);
    uint64_t avail_pa=d->queue.bus_address+
        (uint64_t)((uintptr_t)d->vr.avail-(uintptr_t)queue_va);
    uint64_t used_pa=d->queue.bus_address+
        (uint64_t)((uintptr_t)d->vr.used-(uintptr_t)queue_va);
    rc=nas_virtio_setup_queue(d->transport,0,QUEUE_SIZE,
                              d->queue.bus_address,avail_pa,used_pa);
    if(rc)return rc;
    uint64_t sectors=(uint64_t)nas_virtio_config32(d->transport,0) |
                     ((uint64_t)nas_virtio_config32(d->transport,4)<<32);
    if(!sectors)return -ENODEV;
    bool writable=!(negotiated&FEAT_RO) && (negotiated&FEAT_FLUSH);
    rc=nas_virtio_activate(d->transport);
    if(rc)return rc;
    return nas_port_block_attach(&d->port,names[disk_count],d,sectors,512U,
                                 MAX_TRANSFER/512U,!writable,
                                 NAS_PORT_BLOCK_ZERO,&disk_ops);
}
int nas_virtio_blk_attach(struct nas_virtio_transport *transport) {
    if(!transport||!transport->ops||!transport->dma)return -EINVAL;
    if(disk_count>=MAX_DISKS)return -ENOSPC;
    struct virt_blk *d=&disks[disk_count];
    d->transport=transport;d->dma=transport->dma;
    d->used_index=0;d->failed=false;
    int rc=configure(d);
    if(rc){
        nas_virtio_fail(transport);
        release_pages(d);
        d->transport=NULL;
        return rc;
    }
    disk_count++;
    return 1;
}
unsigned nas_virtio_mmio_blk_count(void){return disk_count;}

/* Registration is transport-independent: scanning may discover any modern
 * VirtIO device, while the bus binds only matching block devices. */
static bool virtio_blk_match(const struct nas_bus_device *dev) {
    return dev->device_id==DEVICE_BLK && dev->context!=NULL;
}
static int virtio_blk_probe(struct nas_bus_device *dev) {
    int rc=nas_virtio_blk_attach((struct nas_virtio_transport *)dev->context);
    return rc==1 ? 0 : (rc==0 ? -ENODEV : rc);
}
static const struct nas_bus_driver virtio_blk_mmio_driver={
    .name="virtio-blk",.bus=NAS_BUS_VIRTIO_MMIO,
    .match=virtio_blk_match,.probe=virtio_blk_probe
};
int nas_virtio_mmio_blk_driver_register(void) {
    return nas_bus_driver_register(&virtio_blk_mmio_driver);
}
