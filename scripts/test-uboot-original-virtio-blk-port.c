/* Real port code + real BlockDevice, with simulated device execution.
 * A host transport mock is NOT a physical-QEMU persistence acceptance. */
#include "../ports/uboot/blk_port.h"
#include "../kernel/errno.h"
#include <virtio/virtio_blk.h>
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define CAPACITY 128U
static _Alignas(4096) uint8_t pages[5][NAS_DMA_PAGE_BYTES];
static bool pages_used[5];
uint64_t nas_hhdm_offset(void) { return 0; }
uint64_t nas_frame_alloc(void) {
    for(unsigned i=0;i<5;i++) if(!pages_used[i]) {
        pages_used[i]=true; return (uint64_t)(uintptr_t)pages[i];
    }
    return 0;
}
bool nas_frame_put(uint64_t addr) {
    for(unsigned i=0;i<5;i++) if(pages_used[i] && addr==(uintptr_t)pages[i]) {
        pages_used[i]=false; return true;
    }
    return false;
}
struct fake_device {
    uint8_t disk[CAPACITY*512];
    struct nas_uboot_block *backend;
    uint64_t features;
    uint32_t status, logical_size;
    unsigned reads,writes,flushes,resets,notifies;
    bool io_error,stall,short_success_read,oversize_success_write;
};
static void reset(void *ctx) {
    struct fake_device *d=ctx; d->status=0;d->resets++;
}
static uint32_t status(void *ctx){return ((struct fake_device*)ctx)->status;}
static void set_status(void *ctx,uint32_t n){((struct fake_device*)ctx)->status=n;}
static uint32_t read_features(void *ctx,unsigned bank){
    return (uint32_t)(((struct fake_device*)ctx)->features>>(bank*32U));
}
static void write_features(void *ctx,unsigned bank,uint32_t x){
    (void)ctx;(void)bank;(void)x;
}
static int setup(void *ctx,unsigned q,unsigned size,uint64_t desc,
                 uint64_t avail,uint64_t used){
    (void)ctx;
    assert(q==0 && size==8 && desc && avail && used);
    return 0;
}
static uint32_t config(void *ctx,uint32_t off) {
    struct fake_device *d=ctx;
    if(off==0)return CAPACITY;
    if(off==4)return 0;
    if(off==offsetof(struct virtio_blk_config,blk_size))return d->logical_size;
    return 0;
}
static void notify(void *ctx,unsigned id) {
    struct fake_device *dev=ctx;
    assert(id==0);dev->notifies++;
    if(dev->stall)return;
    struct nas_vring_queue *q=&dev->backend->ring;
    struct vring *ring=&q->ring;
    const uint16_t slot=(uint16_t)((ring->avail->idx-1U)%ring->num);
    const uint16_t head=ring->avail->ring[slot];
    struct vring_desc *desc=&ring->desc[head];
    assert(desc->len==sizeof(struct virtio_blk_outhdr));
    const struct virtio_blk_outhdr *req=(void *)(uintptr_t)desc->addr;
    const uint32_t kind=req->type;
    assert(req->ioprio==0); /* U-Boot stack data never escapes in the header. */
    struct vring_desc *status_desc;
    uint32_t nwrite=1;
    if(kind!=VIRTIO_BLK_T_FLUSH){
        assert(desc->flags & VRING_DESC_F_NEXT);
        struct vring_desc *data=&ring->desc[desc->next];
        assert(data->flags & VRING_DESC_F_NEXT);
        uint64_t start=req->sector*512ULL;
        assert(start+data->len <= sizeof(dev->disk));
        if(kind==VIRTIO_BLK_T_IN) {
            assert(data->flags & VRING_DESC_F_WRITE);
            memcpy((void *)(uintptr_t)data->addr,dev->disk+start,data->len);
            nwrite+=data->len;dev->reads++;
        }else {
            assert(kind==VIRTIO_BLK_T_OUT && !(data->flags & VRING_DESC_F_WRITE));
            if(!dev->io_error)
                memcpy(dev->disk+start,(void *)(uintptr_t)data->addr,data->len);
            dev->writes++;
        }
        status_desc=&ring->desc[data->next];
    }else {
        status_desc=&ring->desc[desc->next];dev->flushes++;
    }
    assert((status_desc->flags & VRING_DESC_F_WRITE) &&
           !(status_desc->flags & VRING_DESC_F_NEXT) && status_desc->len==1U);
    *(uint8_t *)(uintptr_t)status_desc->addr=dev->io_error?1:0;
    if(dev->short_success_read && kind==VIRTIO_BLK_T_IN && !dev->io_error)
        nwrite=1; /* success status but no bytes of the read payload */
    if(dev->oversize_success_write && kind==VIRTIO_BLK_T_OUT && !dev->io_error)
        nwrite=2; /* impossible status-only write completion */
    ring->used->ring[ring->used->idx%ring->num]=
        (struct vring_used_elem){.id=head,.len=nwrite};
    __atomic_thread_fence(__ATOMIC_RELEASE);
    ring->used->idx++;
}
static const struct nas_virtio_transport_ops ops={
    .reset=reset,.read_status=status,.write_status=set_status,
    .read_features=read_features,.write_features=write_features,
    .setup_queue=setup,.notify_queue=notify,.read_config32=config
};
static void one_case(uint64_t offered,uint32_t size,bool expect_ok) {
    struct fake_device device={.features=offered,.logical_size=size};
    struct nas_uboot_block port={0};device.backend=&port;
    struct nas_virtio_transport t={.context=&device,.ops=&ops,
                                   .dma=&nas_dma_qemu_coherent};
    int rc=nas_uboot_block_attach(&port,&t,"ported-disk");
    if(!expect_ok){assert(rc<0);return;}
    assert(rc==0);
    assert(!(port.parent_priv.features &
             (UINT64_C(1)<<VIRTIO_BLK_F_WRITE_ZEROES)));
    assert(port.port.block.sectors==CAPACITY/(size/512));
    assert(port.port.block.max_sectors_per_io==4096/size);
    struct nas_block_device *blk=nas_block_get("ported-disk");
    assert(blk==&port.port.block);
    assert(nas_uboot_block_detach(&port)==-EBUSY);
    uint8_t value[4096],result[4096];
    for(unsigned i=0;i<4096;i++)value[i]=(uint8_t)(i*7U+3U);
    unsigned nsectors=4096/size;
    if(offered&(UINT64_C(1)<<VIRTIO_BLK_F_RO)) {
        assert(nas_block_write(blk,0,nsectors,value)==-EROFS);
    } else {
        assert(nas_block_write(blk,2,nsectors,value)==0);
        assert(nas_block_flush(blk)==0);
        assert(device.writes==1 && device.flushes==1);
    }
    memset(result,0,sizeof(result));
    assert(nas_block_read(blk,2,nsectors,result)==0);
    if(!(offered&(UINT64_C(1)<<VIRTIO_BLK_F_RO)))
        assert(!memcmp(result,value,4096));
    device.io_error=true;
    assert(nas_block_read(blk,2,1,result)==-EIO);
    assert(!port.poisoned);
    device.io_error=false;
    assert(nas_block_read(blk,2,1,result)==0);
    nas_block_put(blk);
    assert(nas_uboot_block_detach(&port)==0);
    for(unsigned i=0;i<5;i++)assert(!pages_used[i]);
    assert(device.resets>=1);
}

// Hosted test heap used solely by the original U-Boot allocation facade.
#include <stdlib.h>
void *nas_heap_alloc(size_t n){return malloc(n);}
void *nas_heap_calloc(size_t n,size_t s){return calloc(n,s);}
bool nas_heap_free(void *p){free(p);return true;}

int main(void) {
    const uint64_t base=NAS_VIRTIO_F_VERSION_1 |
      (UINT64_C(1)<<VIRTIO_BLK_F_BLK_SIZE)|
      (UINT64_C(1)<<VIRTIO_BLK_F_FLUSH);
    one_case(base,512,true);
    one_case(base|(UINT64_C(1)<<VIRTIO_BLK_F_WRITE_ZEROES),512,true);
    one_case(base,4096,true);
    one_case(base|(UINT64_C(1)<<VIRTIO_BLK_F_RO),512,true);
    one_case(base & ~(UINT64_C(1)<<VIRTIO_BLK_F_FLUSH),512,false);
    one_case(base,1024,true);
    one_case(base,128,false);
    /* Missing completion cannot be converted into success; reset first. */
    {
        struct fake_device stalled={.features=base,.logical_size=512,.stall=true};
        struct nas_uboot_block port={0};stalled.backend=&port;
        struct nas_virtio_transport t={.context=&stalled,.ops=&ops,
                                       .dma=&nas_dma_qemu_coherent};
        assert(nas_uboot_block_attach(&port,&t,"timed-disk")==0);
        struct nas_block_device *blk=nas_block_get("timed-disk");
        uint8_t data[512];
        /* U-Boot's original read API collapses transport timeout to -EIO; it
         * must remain a negative failure and poison the backend. */
        assert(nas_block_read(blk,0,1,data)==-EIO);
        assert(port.poisoned && stalled.resets>=2);
        assert(nas_block_read(blk,0,1,data)==-EIO);
        nas_block_put(blk);
        assert(nas_uboot_block_detach(&port)==0);
        for(unsigned i=0;i<5;i++)assert(!pages_used[i]);
    }
    /* Malformed success completions must not look like valid ext4 bytes. */
    for(unsigned scenario=0;scenario<2;scenario++) {
        struct fake_device malformed={.features=base,.logical_size=512};
        malformed.short_success_read=scenario==0;
        malformed.oversize_success_write=scenario==1;
        struct nas_uboot_block port={0};malformed.backend=&port;
        struct nas_virtio_transport t={.context=&malformed,.ops=&ops,
                                       .dma=&nas_dma_qemu_coherent};
        assert(nas_uboot_block_attach(&port,&t,"malformed-disk")==0);
        struct nas_block_device *blk=nas_block_get("malformed-disk");
        assert(blk);
        uint8_t data[512]={0};
        if(scenario==0)assert(nas_block_read(blk,0,1,data)==-EIO);
        else assert(nas_block_write(blk,0,1,data)==-EIO);
        assert(port.poisoned && malformed.resets>=2);
        nas_block_put(blk);
        assert(nas_uboot_block_detach(&port)==0);
        for(unsigned i=0;i<5;i++)assert(!pages_used[i]);
    }
    puts("[ORIGINAL U-BOOT VIRTIO BLK PORT PASS] real BlockDevice, 512/1024/4096, RO, DMA, read/write/flush, malformed completion fail-closed, EIO, timeout and mount pins");
}
