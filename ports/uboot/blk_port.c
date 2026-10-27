/* SPDX-License-Identifier: GPL-2.0-or-later
 * A compatibility port for the original U-Boot VirtIO-Blk source.
 * U-Boot source below is NOT edited, reimplemented or copied into another
 * request loop. Architecture/transport glue lives outside the driver.
 */
#include "blk_port.h"
#include "../../kernel/mm/heap.h"
#include "../../kernel/errno.h"
#include "../../kernel/lib/string.h"
#include <stddef.h>
#include <stdint.h>
#include <virtio/virtio_ids.h>

/* This include compiles the exact pinned upstream C driver into the kernel.
 * Only U-Boot's device model, virtqueue and allocation APIs are adapted. */
/* Upstream contains a conditional expression mixing signed errno and ulong.
 * Suppress that upstream-only diagnostic without editing upstream bytes. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "../../third_party/uboot/drivers/virtio/virtio_blk.c"
#pragma GCC diagnostic pop

#define NAS_UBOOT_POLL_BUDGET 20000000U
#define NAS_UBOOT_STATUS_BYTE 64U
#define FEATURE(bit) (UINT64_C(1)<<(bit))
static struct nas_uboot_block *backend(struct udevice *d) {
 return d ? (struct nas_uboot_block *)d->context : NULL;
}
static struct nas_uboot_block *queue_backend(struct virtqueue *v) {
 if (!v || !v->transport)return NULL;
 return (struct nas_uboot_block *)((char *)v -
         offsetof(struct nas_uboot_block,vq));
}
int nas_uboot_sprintf(char *dst,const char *fmt,...) {
 if(!dst||!fmt)return -EINVAL;
 va_list ap;va_start(ap,fmt);int n=0;
 if(fmt[0]=='%' && fmt[1]=='s' && !fmt[2]){
     const char *s=va_arg(ap,const char *);
     while(s && *s && n<63)dst[n++]=*s++;
 }else if(!strcmp(fmt,"%04x")){
     unsigned v=va_arg(ap,unsigned);
     for(int i=3;i>=0;--i){unsigned c=(v>>(i*4))&15U;dst[n++]=(char)(c<10?'0'+c:'a'+c-10);}
 }
 dst[n]=0;va_end(ap);return n;
}
int blk_next_free_devnum(int class_id){(void)class_id;return 0;}
void virtio_driver_features_init(struct virtio_dev_priv *p,
 const u32 *f,size_t count,const u32 *x,size_t n) {
 (void)x;(void)n;
 if(!p)return;
 for(size_t i=0;i<count;i++)if(f[i]<64)p->offered |= FEATURE(f[i]);
 /* Filesystem fsync needs the standard VirtIO flush command. */
 p->offered |= FEATURE(VIRTIO_BLK_F_FLUSH) |
               FEATURE(VIRTIO_BLK_F_RO);
}
bool virtio_has_feature(struct udevice *d,unsigned n) {
 struct nas_uboot_block *b=backend(d);
 return b && n<64 && (b->parent_priv.features & FEATURE(n))!=0;
}
void nas_uboot_virtio_cread(struct udevice *d,size_t offset,size_t len,void *dst){
 struct nas_uboot_block *b=backend(d);
 if(!b || !dst || (len!=4 && len!=8)){return;}
 uint64_t v=nas_virtio_config32(&b->transport,(uint32_t)offset);
 if(len==8)v|=(uint64_t)nas_virtio_config32(&b->transport,(uint32_t)(offset+4))<<32;
 if(len==4)*(uint32_t *)dst=(uint32_t)v;
 else *(uint64_t *)dst=v;
}
int virtio_find_vqs(struct udevice *d,int count,struct virtqueue **out){
 struct nas_uboot_block *b=backend(d);
 if(!b || !out || count!=1 || !b->ring.active)return -EINVAL;
 *out=&b->vq;
 return 0;
}
int virtio_reset(struct udevice *d) {
 struct nas_uboot_block *b=backend(d);
 if(!b || !b->transport.ops || !b->transport.ops->reset)return -EINVAL;
 b->transport.ops->reset(b->transport.context);return 0;
}
static void poison(struct virtqueue *v) {
 struct nas_uboot_block *b=queue_backend(v);
 if(!b)return;
 b->poisoned=true;v->poisoned=true;
 nas_virtio_fail(&b->transport);
 if(b->transport.ops && b->transport.ops->reset)
     b->transport.ops->reset(b->transport.context);
}
int virtqueue_add(struct virtqueue *v,struct virtio_sg **sgs,
                  unsigned out,unsigned in) {
 struct nas_uboot_block *b=queue_backend(v);
 if(!b||!sgs||v->poisoned||v->pending||!out||!in ||
    out+in>3U || out+in<2U)return -EIO;
 /* The original upstream request always submits header, optional one
  * payload SG, then status; our 4096-byte cap constrains scatter lists.
  * The adapter owns DMA bounce storage, preserving the API exactly. */
 unsigned n=out+in;
 struct virtio_sg *hdr=sgs[0],*stat=sgs[n-1];
 if(!hdr||!stat||hdr->length!=sizeof(struct virtio_blk_outhdr) ||
    stat->length!=1U || !hdr->addr || !stat->addr)return -EINVAL;
 struct virtio_blk_outhdr h=*(struct virtio_blk_outhdr *)hdr->addr;
 bool flush=h.type==VIRTIO_BLK_T_FLUSH;
 if(flush != (n==2))return -EINVAL;
 struct virtio_sg *data=(n==3)?sgs[1]:NULL;
 if(data && (!data->addr || !data->length ||
             data->length>NAS_DMA_PAGE_BYTES))return -EINVAL;
 if(data && ((h.type==VIRTIO_BLK_T_IN && in!=2) ||
             (h.type==VIRTIO_BLK_T_OUT && out!=2) ||
             (h.type!=VIRTIO_BLK_T_IN && h.type!=VIRTIO_BLK_T_OUT)))
     return -EINVAL;
 memcpy(b->header_dma.cpu,&h,sizeof(h));
 *((uint8_t*)b->header_dma.cpu+NAS_UBOOT_STATUS_BYTE)=VIRTIO_BLK_S_IOERR;
 if(data && h.type==VIRTIO_BLK_T_OUT)
     memcpy(b->payload_dma.cpu,data->addr,data->length);
 int rc=nas_dma_sync(b->transport.dma,&b->header_dma,0,
                 NAS_UBOOT_STATUS_BYTE+1U,NAS_DMA_TO_DEVICE);
 if(rc)goto fail;
 if(data && h.type==VIRTIO_BLK_T_OUT) {
     rc=nas_dma_sync(b->transport.dma,&b->payload_dma,0,
                      data->length,NAS_DMA_TO_DEVICE);
     if(rc)goto fail;
 }
 struct nas_vring_segment segments[3]={
     {b->header_dma.bus_address,sizeof(h),false},
     {b->payload_dma.bus_address,data?data->length:0,
                                h.type==VIRTIO_BLK_T_IN},
     {b->header_dma.bus_address+NAS_UBOOT_STATUS_BYTE,1,true}
 };
 struct nas_vring_segment sg2[2]={segments[0],segments[2]};
 rc=nas_vring_queue_submit(v->ring,flush?sg2:segments,flush?2U:3U,
                           &v->pending_head);
 if(rc)goto fail;
 v->sg_in=stat;
 v->sg_out=data;
 v->out_count=out;v->in_count=in;
 v->pending=true;
 return 0;
fail:
 poison(v);return rc;
}
void virtqueue_kick(struct virtqueue *v) {
 /* nas_vring_queue_submit already notified as required by split-ring
  * semantics; this preserves U-Boot's kick ordering with no double kick. */
 (void)v;
}
void *virtqueue_get_buf(struct virtqueue *v,unsigned *used_len) {
 struct nas_uboot_block *b=queue_backend(v);
 if(!b || !v->pending)return NULL;
 uint32_t written=0;uint16_t id=0;int rc=-EAGAIN;
 for(unsigned i=0;i<NAS_UBOOT_POLL_BUDGET;++i){
     rc=nas_vring_queue_poll(v->ring,&id,&written);
     if(rc!=-EAGAIN)break;
 }
 if(rc || id!=v->pending_head || !written){
     poison(v);v->pending=false;
     return v; /* Status initialized to IOERR by U-Boot. Never report OK. */
 }
 rc=nas_dma_sync(b->transport.dma,&b->header_dma,
                 NAS_UBOOT_STATUS_BYTE,1U,NAS_DMA_FROM_DEVICE);
 if(rc){poison(v);v->pending=false;return v;}
 uint8_t status=*((uint8_t*)b->header_dma.cpu+NAS_UBOOT_STATUS_BYTE);
 *(uint8_t*)v->sg_in->addr=status;
 if(status==VIRTIO_BLK_S_OK && v->sg_out && v->in_count==2){
     rc=nas_dma_sync(b->transport.dma,&b->payload_dma,0,
                      v->sg_out->length,NAS_DMA_FROM_DEVICE);
     if(rc){poison(v);*(uint8_t*)v->sg_in->addr=VIRTIO_BLK_S_IOERR;}
     else memcpy(v->sg_out->addr,b->payload_dma.cpu,v->sg_out->length);
 }
 if(used_len)*used_len=written;
 v->pending=false;
 return v;
}
static int submit_flush(struct nas_uboot_block *b){
 struct virtio_blk_outhdr h={.type=VIRTIO_BLK_T_FLUSH};
 uint8_t status=VIRTIO_BLK_S_IOERR;
 struct virtio_sg hs={&h,sizeof(h)},ss={&status,1};
 struct virtio_sg *sgs[2]={&hs,&ss};
 int rc=virtqueue_add(&b->vq,sgs,1,1);
 if(rc)return rc;
 virtqueue_kick(&b->vq);
 if(!virtqueue_get_buf(&b->vq,NULL)||status!=VIRTIO_BLK_S_OK)return -EIO;
 return 0;
}
static int64_t read_blocks(void *ctx,uint64_t lba,uint32_t count,void *dst){
 struct nas_uboot_block *b=ctx;
 if(__atomic_exchange_n(&b->busy,true,__ATOMIC_ACQUIRE))return -EBUSY;
 int64_t rc=b->poisoned?-EIO:
      (int64_t)uboot_virtio_blk.ops->read(&b->udev,lba,count,dst);
 if(rc!=(int64_t)count)rc=-EIO;
 __atomic_store_n(&b->busy,false,__ATOMIC_RELEASE);return rc;
}
static int64_t write_blocks(void *ctx,uint64_t lba,uint32_t count,const void *src){
 struct nas_uboot_block *b=ctx;
 if(__atomic_exchange_n(&b->busy,true,__ATOMIC_ACQUIRE))return -EBUSY;
 int64_t rc=b->poisoned?-EIO:
      (int64_t)uboot_virtio_blk.ops->write(&b->udev,lba,count,src);
 if(rc!=(int64_t)count)rc=-EIO;
 __atomic_store_n(&b->busy,false,__ATOMIC_RELEASE);return rc;
}
static int flush_blocks(void *ctx){
 struct nas_uboot_block *b=ctx;
 if(!b->flush_supported)return -EOPNOTSUPP;
 if(__atomic_exchange_n(&b->busy,true,__ATOMIC_ACQUIRE))return -EBUSY;
 int rc=b->poisoned?-EIO:submit_flush(b);
 __atomic_store_n(&b->busy,false,__ATOMIC_RELEASE);return rc;
}
static const struct nas_port_block_ops block_ops={
 .read=read_blocks,.write=write_blocks,.flush=flush_blocks
};
int nas_uboot_block_attach(struct nas_uboot_block *b,
 const struct nas_virtio_transport *tr,const char *name){
 if(!b||!tr||!name||!name[0]||!tr->dma||!tr->dma->coherent ||
    b->port.block.registered||b->ring.active)return -EINVAL;
 b->transport=*tr;b->poisoned=false;b->busy=false;
 b->parent_priv=(struct virtio_dev_priv){.vendor=0};
 b->parent=(struct udevice){.name=name,.uclass_priv=&b->parent_priv};
 b->udev=(struct udevice){.name=name,.parent=&b->parent,.context=b,.plat=&b->desc};
 b->udev.priv=nas_heap_calloc(1,uboot_virtio_blk.priv_auto);
 if(!b->udev.priv)return -ENOMEM;
 int rc=uboot_virtio_blk.bind(&b->udev);
 if(rc)goto fail;
 uint64_t supported=0;
 rc=nas_virtio_negotiate(&b->transport,0,b->parent_priv.offered,
                          &supported);
 if(rc)goto fail;
 b->parent_priv.features=supported;
 bool ro=(supported & FEATURE(VIRTIO_BLK_F_RO))!=0;
 b->flush_supported=(supported & FEATURE(VIRTIO_BLK_F_FLUSH))!=0;
 if(!ro && !b->flush_supported){rc=-EOPNOTSUPP;goto fail;}
 rc=nas_dma_alloc_page(b->transport.dma,&b->header_dma);
 if(rc)goto fail;
 rc=nas_dma_alloc_page(b->transport.dma,&b->payload_dma);
 if(rc)goto fail;
 rc=nas_vring_queue_init(&b->ring,&b->transport,0,8);
 if(rc)goto fail;
 b->vq=(struct virtqueue){.ring=&b->ring,.transport=&b->transport,
                          .header=&b->header_dma,.payload=&b->payload_dma};
 rc=nas_virtio_activate(&b->transport);
 if(rc)goto fail;
 rc=uboot_virtio_blk.probe(&b->udev);
 if(rc)goto fail;
 if(!b->desc.lba || b->desc.blksz<512U || b->desc.blksz>4096U ||
    b->desc.blksz&(b->desc.blksz-1U)){rc=-EINVAL;goto fail;}
 struct virtio_blk_priv *priv=dev_get_priv(&b->udev);
 if(priv->size_max<NAS_DMA_PAGE_BYTES || !priv->seg_max){
     /* Small scatter limits require multi-segment bounce handling. */
     rc=-EOPNOTSUPP;goto fail;
 }
 rc=nas_port_block_attach(&b->port,name,b,b->desc.lba,
      (uint32_t)b->desc.blksz,NAS_DMA_PAGE_BYTES/(uint32_t)b->desc.blksz,
      ro,NAS_PORT_BLOCK_SECTORS,&block_ops);
 if(!rc)return 0;
fail:
 if(b->ring.active)(void)nas_vring_queue_close(&b->ring);
 if(b->payload_dma.cpu)(void)nas_dma_free_page(b->transport.dma,&b->payload_dma);
 if(b->header_dma.cpu)(void)nas_dma_free_page(b->transport.dma,&b->header_dma);
 if(b->transport.ops && b->transport.ops->reset)
      b->transport.ops->reset(b->transport.context);
 if(b->udev.priv){(void)nas_heap_free(b->udev.priv);b->udev.priv=NULL;}
 return rc;
}
int nas_uboot_block_detach(struct nas_uboot_block *b){
 if(!b)return -EINVAL;
 if(__atomic_load_n(&b->busy,__ATOMIC_ACQUIRE))return -EBUSY;
 int rc=nas_port_block_detach(&b->port);
 if(rc)return rc;
 rc=nas_vring_queue_close(&b->ring);
 if(rc)return rc;
 int e1=nas_dma_free_page(b->transport.dma,&b->header_dma);
 int e2=nas_dma_free_page(b->transport.dma,&b->payload_dma);
 if(b->udev.priv){(void)nas_heap_free(b->udev.priv);b->udev.priv=NULL;}
 return e1?e1:e2;
}
