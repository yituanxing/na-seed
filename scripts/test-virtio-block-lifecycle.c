/* Real VirtIO-Blk request engine, bus, BlockDevice and DMA lifetime. */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <virtio/virtio_ring.h>
#include <virtio/virtio_blk.h>
#include "../kernel/drivers/block/virtio_mmio_blk.h"
#include "../kernel/drivers/core/bus.h"
#include "../kernel/drivers/port/dma.h"
#include "../kernel/block/device.h"
#include "../kernel/errno.h"
#define NPAGES 12
static uint8_t pages[NPAGES][NAS_DMA_PAGE_BYTES] __attribute__((aligned(4096)));
static bool busy[NPAGES];
static unsigned allocs,frees;
static bool alloc_fail;
int nas_dma_alloc_page(const struct nas_dma_provider *p,struct nas_dma_page *pg) {
    if(!p||!pg)return -EINVAL;
    *pg=(struct nas_dma_page){0};
    if(alloc_fail)return -ENOMEM;
    for(unsigned i=0;i<NPAGES;i++)if(!busy[i]) {
        busy[i]=true;allocs++;
        memset(pages[i],0,NAS_DMA_PAGE_BYTES);
        pg->cpu=pages[i];pg->bus_address=(uint64_t)(uintptr_t)pages[i];
        pg->bytes=NAS_DMA_PAGE_BYTES;
        return 0;
    }
    return -ENOMEM;
}
int nas_dma_free_page(const struct nas_dma_provider *p,struct nas_dma_page *pg) {
    if(!p||!pg||!pg->cpu)return -EINVAL;
    for(unsigned i=0;i<NPAGES;i++)if(pg->cpu==pages[i] && busy[i]) {
        busy[i]=false;frees++;*pg=(struct nas_dma_page){0};
        return 0;
    }
    return -EINVAL;
}
int nas_dma_sync(const struct nas_dma_provider *p,struct nas_dma_page *pg,
                 uint32_t off,uint32_t n,enum nas_dma_direction way) {
    return p&&p->coherent&&pg&&pg->cpu && off<=pg->bytes &&
           n<=pg->bytes-off && (way==NAS_DMA_TO_DEVICE ||
           way==NAS_DMA_FROM_DEVICE) ? 0 : -EINVAL;
}
static const struct nas_dma_provider dma={.coherent=true};
struct device {uint64_t features,accepted,desc,avail,used;
    uint32_t status;uint8_t sectors[32*512];
    unsigned io,flushes,resets;bool fail_setup;};
static void reset(void *p){struct device *d=p;d->status=0;d->resets++;}
static uint32_t status(void *p){return ((struct device *)p)->status;}
static void set_status(void *p,uint32_t s){((struct device *)p)->status=s;}
static uint32_t feature(void *p,unsigned bank){
    return (uint32_t)(((struct device *)p)->features>>(32U*bank));
}
static void set_feature(void *p,unsigned bank,uint32_t bits){
    struct device *d=p;uint64_t mask=UINT64_C(0xffffffff)<<(32U*bank);
    d->accepted=(d->accepted&~mask)|((uint64_t)bits<<(32U*bank));
}
static int queue(void *p,unsigned id,unsigned count,uint64_t desc,
                 uint64_t avail,uint64_t used) {
    struct device *d=p;
    if(d->fail_setup)return -EIO;
    if(id || count!=8 || !desc || !avail || !used)return -EINVAL;
    d->desc=desc;d->avail=avail;d->used=used;return 0;
}
static void notify(void *p,unsigned id){
    struct device *d=p;
    assert(id==0);
    struct vring_desc *v=(void *)(uintptr_t)d->desc;
    struct vring_avail *a=(void *)(uintptr_t)d->avail;
    struct vring_used *u=(void *)(uintptr_t)d->used;
    unsigned head=a->ring[(a->idx-1U)%8U];
    assert(head==0);
    const struct virtio_blk_outhdr *hdr=(void *)(uintptr_t)v[0].addr;
    uint8_t *result=(void *)(uintptr_t)v[2].addr;
    unsigned size=0;
    if(hdr->type==VIRTIO_BLK_T_IN || hdr->type==VIRTIO_BLK_T_OUT) {
        size=v[1].len;
        assert(hdr->sector<32 && size<=sizeof(d->sectors)-
               hdr->sector*512U);
        void *data=(void *)(uintptr_t)v[1].addr;
        uint8_t *disk=d->sectors+(size_t)hdr->sector*512U;
        if(hdr->type==VIRTIO_BLK_T_IN)memcpy(data,disk,size);
        else memcpy(disk,data,size);
        d->io++;
    }else{assert(hdr->type==VIRTIO_BLK_T_FLUSH);d->flushes++;}
    *result=VIRTIO_BLK_S_OK;
    u->ring[u->idx%8U]=(struct vring_used_elem){.id=0,.len=size};
    u->idx++;
}
static uint32_t config(void *p,uint32_t off){(void)p;return off==0?32:0;}
static const struct nas_virtio_transport_ops ops={
    .reset=reset,.read_status=status,.write_status=set_status,
    .read_features=feature,.write_features=set_feature,
    .setup_queue=queue,.notify_queue=notify,.read_config32=config
};
static struct device a,b,c,fail;
static struct nas_virtio_transport ta={.context=&a,.ops=&ops,.dma=&dma};
static struct nas_virtio_transport tb={.context=&b,.ops=&ops,.dma=&dma};
static struct nas_virtio_transport tc={.context=&c,.ops=&ops,.dma=&dma};
static struct nas_virtio_transport tf={.context=&fail,.ops=&ops,.dma=&dma};
static struct nas_bus_device ma={.bus=NAS_BUS_VIRTIO_MMIO,.device_id=VIRTIO_ID_BLOCK,
    .resource=(volatile void *)0x1000,.context=&ta};
static struct nas_bus_device pb={.bus=NAS_BUS_PCI,.device_id=VIRTIO_ID_BLOCK,
    .resource=(volatile void *)0x2000,.context=&tb};
static struct nas_bus_device mc={.bus=NAS_BUS_VIRTIO_MMIO,.device_id=VIRTIO_ID_BLOCK,
    .resource=(volatile void *)0x3000,.context=&tc};
static struct nas_bus_device bad={.bus=NAS_BUS_PCI,.device_id=VIRTIO_ID_BLOCK,
    .resource=(volatile void *)0x4000,.context=&tf};
int main(void) {
    a.features=b.features=c.features=fail.features=
      NAS_VIRTIO_F_VERSION_1 | (UINT64_C(1)<<VIRTIO_BLK_F_FLUSH);
    assert(nas_virtio_blk_driver_register()==0);
    assert(nas_bus_device_register(&ma)==0);
    assert(nas_bus_device_register(&pb)==0);
    assert(nas_virtio_blk_count()==2 && allocs-frees==6);
    assert(nas_virtio_blk_attach(&ta)==-EEXIST);
    struct nas_block_device *one=nas_block_get("virtio0");
    struct nas_block_device *two=nas_block_get("virtio1");
    assert(one && two && one!=two);
    uint8_t source[512],got[512]={0};
    for(unsigned i=0;i<sizeof(source);i++)source[i]=(uint8_t)(i*7U);
    assert(nas_block_write(one,3,1,source)==0);
    assert(nas_block_read(one,3,1,got)==0);
    assert(!memcmp(got,source,sizeof(got)) && a.io==2);
    assert(nas_block_flush(one)==0 && a.flushes==1);
    assert(nas_bus_device_unbind(&ma)==-EBUSY && ma.driver);
    nas_block_put(one);
    assert(nas_bus_device_unbind(&ma)==0);
    assert(!ma.driver && nas_block_get("virtio0")==NULL);
    assert(nas_virtio_blk_count()==1 && allocs-frees==3);
    assert(a.resets>=2 && nas_bus_device_unregister(&ma)==0);
    assert(nas_bus_device_register(&mc)==0);
    one=nas_block_get("virtio0");
    assert(one && two && one!=two && nas_virtio_blk_count()==2);
    assert(nas_block_write(one,1,1,source)==0);
    assert(nas_block_read(one,1,1,got)==0 && !memcmp(got,source,sizeof(got)));
    nas_block_put(one);
    assert(nas_bus_device_unbind(&mc)==0 && nas_bus_device_unregister(&mc)==0);
    nas_block_put(two);
    assert(nas_bus_device_unbind(&pb)==0 && nas_bus_device_unregister(&pb)==0);
    assert(nas_virtio_blk_count()==0 && allocs==frees);
    fail.fail_setup=true;
    assert(nas_bus_device_register(&bad)==-EIO);
    assert(!bad.registered && nas_virtio_blk_count()==0 && allocs==frees);
    fail.fail_setup=false;
    assert(nas_bus_device_register(&bad)==0 && nas_virtio_blk_count()==1);
    assert(nas_bus_device_unbind(&bad)==0 && nas_bus_device_unregister(&bad)==0);
    assert(nas_virtio_blk_count()==0 && allocs==frees);
    puts("[VIRTIO BLOCK LIFECYCLE PASS] real request, MMIO/PCI, pins, reset, DMA, slot reuse, failed probe");
    return 0;
}
