/* SPDX-License-Identifier: GPL-2.0-or-later
 * Exact pinned U-Boot VirtIO-Net driver; no forked queue or Ethernet logic.
 * Only the VirtIO DMA bounce boundary and NaSeed device model live here. */
#include "net_port.h"
#include "../../kernel/mm/heap.h"
#include "../../kernel/errno.h"
#include "../../kernel/lib/string.h"
#include <stddef.h>
#include <stdint.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "../../third_party/uboot/drivers/virtio/virtio_net.c"
#pragma GCC diagnostic pop

#define NAS_NET_TX_POLL_BUDGET 2000000U
#define NAS_NET_TX_FRAME_OFFSET 64U
#define NET_FEATURE(bit) (UINT64_C(1)<<(bit))

static struct nas_uboot_net *net_owner(struct virtqueue *vq){
 return vq ? (struct nas_uboot_net *)vq->owner : NULL;
}
static void net_poison(struct nas_uboot_net *n){
 if(!n || n->poisoned)return;
 n->poisoned=true;n->rx_vq.poisoned=true;n->tx_vq.poisoned=true;
 nas_virtio_fail(&n->transport);
 if(n->transport.ops && n->transport.ops->reset)
     n->transport.ops->reset(n->transport.context);
}
/* The U-Boot driver keeps CPU-only rx_buff[][]; never give those stack/heap
 * addresses to VirtIO. Map each original buffer onto a DMA-owned page. */
static int rx_slot(struct nas_uboot_net *n,const void *addr){
 struct virtio_net_priv *priv=dev_get_priv(&n->udev);
 if(!priv)return -1;
 for(unsigned i=0;i<NAS_UBOOT_NET_RX_SLOTS;i++)
     if(addr==(const void *)priv->rx_buff[i])return (int)i;
 return -1;
}
static int net_queue_add(struct virtqueue *vq,struct virtio_sg **sgs,
                         unsigned out,unsigned in){
 struct nas_uboot_net *n=net_owner(vq);
 if(!n || n->poisoned || !sgs)return -EIO;
 if(vq==&n->rx_vq){
     if(out || in!=1 || !sgs[0] ||
        sgs[0]->length!=NAS_UBOOT_NET_RX_BYTES)return -EINVAL;
     int slot=rx_slot(n,sgs[0]->addr);
     if(slot<0 || n->rx_queued[slot])return -EINVAL;
     struct nas_vring_segment seg={n->rx_dma[slot].bus_address,
                                   NAS_UBOOT_NET_RX_BYTES,true};
     uint16_t head=0;
     int rc=nas_dma_sync(n->transport.dma,&n->rx_dma[slot],0,
                         NAS_UBOOT_NET_RX_BYTES,NAS_DMA_FROM_DEVICE);
     if(!rc)rc=nas_vring_queue_submit(&n->rx_ring,&seg,1,&head);
     if(rc){net_poison(n);return rc;}
     if(head>=NAS_UBOOT_NET_RX_RING_SIZE || n->rx_slot_by_head[head]){
         net_poison(n);return -EIO;
     }
     n->rx_slot_by_head[head]=(uint16_t)(slot+1);
     n->rx_queued[slot]=true;
     n->rx_submitted++;
     return 0;
 }
 if(vq!=&n->tx_vq || in || out!=2 || n->tx_pending ||
    !sgs[0] || !sgs[1] || !sgs[0]->addr || !sgs[1]->addr ||
    sgs[0]->length!=sizeof(struct virtio_net_hdr_v1) ||
    sgs[1]->length<14U || sgs[1]->length>1518U)return -EINVAL;
 memcpy(n->tx_dma.cpu,sgs[0]->addr,sgs[0]->length);
 memcpy((uint8_t *)n->tx_dma.cpu+NAS_NET_TX_FRAME_OFFSET,
        sgs[1]->addr,sgs[1]->length);
 int rc=nas_dma_sync(n->transport.dma,&n->tx_dma,0,
         NAS_NET_TX_FRAME_OFFSET+sgs[1]->length,NAS_DMA_TO_DEVICE);
 if(rc){net_poison(n);return rc;}
 struct nas_vring_segment segments[2]={
     {n->tx_dma.bus_address,(uint32_t)sgs[0]->length,false},
     {n->tx_dma.bus_address+NAS_NET_TX_FRAME_OFFSET,
      (uint32_t)sgs[1]->length,false}
 };
 rc=nas_vring_queue_submit(&n->tx_ring,segments,2,&n->tx_head);
 if(rc){net_poison(n);return rc;}
 n->tx_pending=true;
 return 0;
}
static void net_queue_kick(struct virtqueue *vq){
 /* Shared split-ring submit has already notified; no duplicate kick. */
 (void)vq;
}
static void *net_queue_get_buf(struct virtqueue *vq,unsigned *used_len){
 struct nas_uboot_net *n=net_owner(vq);
 if(!n || n->poisoned)return NULL;
 uint16_t head=0;uint32_t bytes=0;
 if(vq==&n->tx_vq){
     if(!n->tx_pending)return NULL;
     int rc=-EAGAIN;
     /* Upstream send() loops forever; consume its request in one bounded
      * get_buf(), then return a sentinel and force the caller to report EIO. */
     for(unsigned k=0;k<NAS_NET_TX_POLL_BUDGET;k++){
         rc=nas_vring_queue_poll(&n->tx_ring,&head,&bytes);
         if(rc!=-EAGAIN)break;
     }
     n->tx_pending=false;
     if(rc || head!=n->tx_head || bytes){
         net_poison(n);return vq;
     }
     if(used_len)*used_len=bytes;
     return vq;
 }
 if(vq!=&n->rx_vq)return NULL;
 int rc=nas_vring_queue_poll(&n->rx_ring,&head,&bytes);
 if(rc==-EAGAIN)return NULL;
 if(rc || head>=NAS_UBOOT_NET_RX_RING_SIZE ||
    !n->rx_slot_by_head[head]){
     net_poison(n);return NULL;
 }
 unsigned slot=n->rx_slot_by_head[head]-1U;
 n->rx_slot_by_head[head]=0;
 n->rx_queued[slot]=false;
 if(bytes<sizeof(struct virtio_net_hdr_v1)+14U ||
    bytes>NAS_UBOOT_NET_RX_BYTES ||
    nas_dma_sync(n->transport.dma,&n->rx_dma[slot],0,bytes,
                 NAS_DMA_FROM_DEVICE)){
     net_poison(n);return NULL;
 }
 struct virtio_net_priv *priv=dev_get_priv(&n->udev);
 memcpy(priv->rx_buff[slot],n->rx_dma[slot].cpu,bytes);
 if(used_len)*used_len=bytes;
 return priv->rx_buff[slot];
}
static const struct nas_uboot_vq_ops net_vq_bridge={
 .add=net_queue_add,.kick=net_queue_kick,.get_buf=net_queue_get_buf
};
static const struct eth_ops *upstream_ops(void){
 return (const struct eth_ops *)uboot_virtio_net.ops;
}
static int net_transmit(void *ctx,const uint8_t *frame,uint32_t bytes){
 struct nas_uboot_net *n=ctx;
 if(__atomic_exchange_n(&n->busy,true,__ATOMIC_ACQUIRE))return -EBUSY;
 int rc=n->poisoned?-EIO:upstream_ops()->send(&n->udev,(void *)frame,(int)bytes);
 if(n->poisoned)rc=-EIO;
 __atomic_store_n(&n->busy,false,__ATOMIC_RELEASE);
 return rc;
}
static int net_poll(void *ctx,uint32_t budget,
                    nas_netdev_rx receive,void *consumer){
 struct nas_uboot_net *n=ctx;
 if(__atomic_exchange_n(&n->busy,true,__ATOMIC_ACQUIRE))return -EBUSY;
 int count=0,rc=0;
 while((uint32_t)count<budget && !n->poisoned){
     uchar *packet=NULL;
     int len=upstream_ops()->recv(&n->udev,0,&packet);
     if(len==-EAGAIN)break;
     if(len<14 || len>1518 || !packet){net_poison(n);rc=-EIO;break;}
     rc=receive(consumer,packet,(uint32_t)len);
     (void)upstream_ops()->free_pkt(&n->udev,packet,len);
     if(n->poisoned){rc=-EIO;break;}
     if(rc)break;
     count++;
 }
 if(n->poisoned)rc=-EIO;
 __atomic_store_n(&n->busy,false,__ATOMIC_RELEASE);
 return rc?rc:count;
}
static bool net_link_up(void *ctx){
 return !((struct nas_uboot_net *)ctx)->poisoned;
}
static const struct nas_netdev_ops net_ops={
 .transmit=net_transmit,.poll=net_poll,.link_up=net_link_up
};
int nas_uboot_net_attach(struct nas_uboot_net *n,
 const struct nas_virtio_transport *tr,const char *name){
 if(!n || !tr || !name || !name[0] || !tr->dma || !tr->dma->coherent ||
    n->netdev.registered || n->rx_ring.active || n->tx_ring.active)
     return -EINVAL;
 n->transport=*tr;n->busy=false;n->poisoned=false;
 n->rx_dma_count=0;n->rx_submitted=0;n->tx_pending=false;
 memset(n->rx_slot_by_head,0,sizeof(n->rx_slot_by_head));
 memset(n->rx_queued,0,sizeof(n->rx_queued));
 n->parent_priv=(struct virtio_dev_priv){.vendor=0,
    .transport=&n->transport,.queues=n->queue_refs,.queue_count=2};
 n->parent=(struct udevice){.name=name,.uclass_priv=&n->parent_priv};
 n->udev=(struct udevice){.name=name,.parent=&n->parent,
                          .context=n,.plat=&n->eth};
 n->udev.priv=nas_heap_calloc(1,uboot_virtio_net.priv_auto);
 if(!n->udev.priv)return -ENOMEM;
 int rc=uboot_virtio_net.bind(&n->udev);
 if(rc)goto fail;
 rc=nas_virtio_negotiate(&n->transport,NET_FEATURE(VIRTIO_NET_F_MAC),
                         n->parent_priv.offered,&n->parent_priv.features);
 if(rc)goto fail;
 rc=nas_dma_alloc_page(n->transport.dma,&n->tx_dma);
 if(rc)goto fail;
 for(unsigned i=0;i<NAS_UBOOT_NET_RX_SLOTS;i++){
     rc=nas_dma_alloc_page(n->transport.dma,&n->rx_dma[i]);
     if(rc)goto fail;
     n->rx_dma_count++;
 }
 rc=nas_vring_queue_init(&n->rx_ring,&n->transport,0,
                         NAS_UBOOT_NET_RX_RING_SIZE);
 if(rc)goto fail;
 rc=nas_vring_queue_init(&n->tx_ring,&n->transport,1,8);
 if(rc)goto fail;
 n->rx_vq=(struct virtqueue){.bridge=&net_vq_bridge,.owner=n,
                            .ring=&n->rx_ring,.transport=&n->transport};
 n->tx_vq=(struct virtqueue){.bridge=&net_vq_bridge,.owner=n,
                            .ring=&n->tx_ring,.transport=&n->transport};
 n->queue_refs[0]=&n->rx_vq;n->queue_refs[1]=&n->tx_vq;
 rc=nas_virtio_activate(&n->transport);
 if(rc)goto fail;
 rc=uboot_virtio_net.probe(&n->udev);
 if(rc)goto fail;
 rc=upstream_ops()->read_rom_hwaddr(&n->udev);
 if(rc)goto fail;
 rc=upstream_ops()->start(&n->udev);
 if(rc)goto fail;
 if(n->poisoned || n->rx_submitted!=NAS_UBOOT_NET_RX_SLOTS){
     rc=-EIO;goto fail;
 }
 n->netdev=(struct nas_netdev){.name=name,.backend=n,
     .mtu=1500,.ops=&net_ops};
 memcpy(n->netdev.mac,n->eth.enetaddr,sizeof(n->eth.enetaddr));
 rc=nas_netdev_register(&n->netdev);
 if(!rc)return 0;
fail:
 if(n->rx_ring.active)(void)nas_vring_queue_close(&n->rx_ring);
 if(n->tx_ring.active)(void)nas_vring_queue_close(&n->tx_ring);
 for(unsigned i=0;i<n->rx_dma_count;i++)
     (void)nas_dma_free_page(n->transport.dma,&n->rx_dma[i]);
 n->rx_dma_count=0;
 if(n->tx_dma.cpu)(void)nas_dma_free_page(n->transport.dma,&n->tx_dma);
 if(n->transport.ops && n->transport.ops->reset)
     n->transport.ops->reset(n->transport.context);
 if(n->udev.priv){(void)nas_heap_free(n->udev.priv);n->udev.priv=NULL;}
 return rc;
}
int nas_uboot_net_detach(struct nas_uboot_net *n){
 if(!n)return -EINVAL;
 if(__atomic_load_n(&n->busy,__ATOMIC_ACQUIRE))return -EBUSY;
 /* First notify the sole stack consumer and prohibit new netdev pins.
  * A retry after the lwIP tcpip_thread releases its Path-like device pin
  * is required before the original U-Boot DMA/virtqueues can be reclaimed. */
 int begin=nas_netdev_begin_remove(&n->netdev);
 if(begin)return begin;
 int rc=nas_netdev_unregister(&n->netdev);
 if(rc)return rc;
 upstream_ops()->stop(&n->udev);
 int e1=nas_vring_queue_close(&n->rx_ring);
 int e2=nas_vring_queue_close(&n->tx_ring);
 int e3=0;
 for(unsigned i=0;i<n->rx_dma_count;i++){
     int e=nas_dma_free_page(n->transport.dma,&n->rx_dma[i]);
     if(e && !e3)e3=e;
 }
 n->rx_dma_count=0;
 int e4=nas_dma_free_page(n->transport.dma,&n->tx_dma);
 if(n->udev.priv){(void)nas_heap_free(n->udev.priv);n->udev.priv=NULL;}
 return e1?e1:e2?e2:e3?e3:e4;
}
