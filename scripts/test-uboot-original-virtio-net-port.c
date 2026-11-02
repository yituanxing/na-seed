/* Host simulation exercises original U-Boot source via NaSeed netdev/DMA.
 * It is NOT hardware-QEMU or lwIP TCP/IP acceptance. */
#include "../ports/uboot/net_port.h"
#include "../ports/uboot/virtio_net_bus.h"
#include "../kernel/drivers/core/bus.h"
#include "../kernel/errno.h"
#include <virtio/virtio_ids.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PAGES 80U /* two simultaneously live original 33-page U-Boot NICs */
static _Alignas(4096) uint8_t pages[PAGES][NAS_DMA_PAGE_BYTES];
static bool used[PAGES];
uint64_t nas_hhdm_offset(void){return 0;}
uint64_t nas_frame_alloc(void){
 for(unsigned i=0;i<PAGES;i++)if(!used[i]){
     used[i]=true;return (uint64_t)(uintptr_t)pages[i];
 }
 return 0;
}
bool nas_frame_put(uint64_t frame){
 for(unsigned i=0;i<PAGES;i++)if(used[i] && (uintptr_t)pages[i]==frame){
     used[i]=false;return true;
 }
 return false;
}
void *nas_heap_alloc(size_t n){return malloc(n);}
void *nas_heap_calloc(size_t n,size_t sz){return calloc(n,sz);}
bool nas_heap_free(void *p){free(p);return true;}

struct fake_nic {
 struct nas_uboot_net *backend;
 uint64_t features;
 uint8_t mac[6];
 uint32_t status;
 unsigned resets,rx_notify,tx_notify,tx_count,rx_cursor;
 bool stall_tx;
 uint8_t tx_frame[1518];
 unsigned tx_length;
};
static void reset(void *p){
 struct fake_nic *n=p;n->status=0;n->resets++;
}
static uint32_t status(void *p){return ((struct fake_nic *)p)->status;}
static void set_status(void *p,uint32_t s){((struct fake_nic *)p)->status=s;}
static uint32_t features(void *p,unsigned bank){
 return (uint32_t)(((struct fake_nic *)p)->features>>(32*bank));
}
static void write_features(void *p,unsigned bank,uint32_t bits){
 (void)p;(void)bank;(void)bits;
}
static int setup(void *p,unsigned q,unsigned size,uint64_t desc,
                 uint64_t avail,uint64_t complete){
 (void)p;
 assert(q<2 && size==(q?8U:NAS_UBOOT_NET_RX_RING_SIZE));
 assert(desc && avail && complete);
 return 0;
}
static uint32_t config(void *p,uint32_t off){
 struct fake_nic *n=p;
 assert(off==0 || off==4);
 uint32_t value=0;
 for(unsigned i=0;i<4 && off+i<6;i++)
     value|=(uint32_t)n->mac[off+i]<<(8*i);
 return value;
}
static void notify(void *p,unsigned qid){
 struct fake_nic *n=p;
 if(qid==0){n->rx_notify++;return;}
 assert(qid==1);
 n->tx_notify++;
 if(n->stall_tx)return;
 struct vring *r=&n->backend->tx_ring.ring;
 uint16_t head=r->avail->ring[(r->avail->idx-1U)%r->num];
 struct vring_desc *h=&r->desc[head];
 assert(h->len==12 && (h->flags & VRING_DESC_F_NEXT));
 const uint8_t *header=(const void *)(uintptr_t)h->addr;
 for(unsigned i=0;i<h->len;i++)assert(header[i]==0);
 struct vring_desc *data=&r->desc[h->next];
 assert(!(data->flags & VRING_DESC_F_WRITE) &&
        !(data->flags & VRING_DESC_F_NEXT) && data->len<=1518);
 memcpy(n->tx_frame,(const void *)(uintptr_t)data->addr,data->len);
 n->tx_length=data->len;n->tx_count++;
 r->used->ring[r->used->idx%r->num]=
     (struct vring_used_elem){.id=head,.len=0};
 r->used->idx++;
}
static const struct nas_virtio_transport_ops ops={
 .reset=reset,.read_status=status,.write_status=set_status,
 .read_features=features,.write_features=write_features,
 .setup_queue=setup,.notify_queue=notify,.read_config32=config
};
static const uint8_t frame[60]={
 0x52,0x54,0x00,0x12,0x34,0x56,0x02,0x00,0x00,0x00,0x00,0x01,
 0x08,0x00,0x45,0x00,0x00,0x2e
};
static void inject(struct fake_nic *n,uint32_t written){
 struct vring *r=&n->backend->rx_ring.ring;
 assert(n->rx_cursor<r->avail->idx);
 uint16_t head=r->avail->ring[n->rx_cursor%r->num];
 n->rx_cursor++;
 struct vring_desc *d=&r->desc[head];
 assert((d->flags&VRING_DESC_F_WRITE) && d->len==1526);
 uint8_t *dst=(void *)(uintptr_t)d->addr;
 memset(dst,0,12);
 memcpy(dst+12,frame,sizeof(frame));
 r->used->ring[r->used->idx%r->num]=
     (struct vring_used_elem){.id=head,.len=written};
 r->used->idx++;
}
static int receive(void *ctx,const uint8_t *data,uint32_t bytes){
 unsigned *calls=ctx;
 assert(bytes==sizeof(frame) && !memcmp(data,frame,sizeof(frame)));
 (*calls)++;
 return 0;
}
static void all_pages_freed(void){
 for(unsigned i=0;i<PAGES;i++)assert(!used[i]);
}
int main(void){
 struct nas_uboot_net nic={0};
 struct fake_nic fake={.backend=&nic,
   .features=NAS_VIRTIO_F_VERSION_1|(UINT64_C(1)<<5),
   .mac={0x52,0x54,0,0x12,0x34,0x56}};
 struct nas_virtio_transport tr={.context=&fake,.ops=&ops,
                                 .dma=&nas_dma_qemu_coherent};
 assert(nas_uboot_net_attach(&nic,&tr,"eth-uboot")==0);
 assert(nic.rx_submitted==32 && fake.rx_notify==32);
 assert(!memcmp(nic.netdev.mac,fake.mac,6));
 struct nas_netdev *dev=nas_netdev_get("eth-uboot");
 assert(dev==&nic.netdev && nas_netdev_link_up(dev));
 assert(nas_netdev_transmit(dev,frame,sizeof(frame))==0);
 assert(fake.tx_count==1 && fake.tx_length==sizeof(frame));
 assert(!memcmp(fake.tx_frame,frame,sizeof(frame)));
 unsigned calls=0;
 assert(nas_netdev_poll(dev,4,receive,&calls)==0);
 inject(&fake,sizeof(frame)+12U);
 assert(nas_netdev_poll(dev,4,receive,&calls)==1 && calls==1);
 assert(nic.rx_submitted==33);
 assert(nas_netdev_poll(dev,4,receive,&calls)==0);
 /* malformed completion poisons device, never leaks stale packet data. */
 inject(&fake,12);
 assert(nas_netdev_poll(dev,4,receive,&calls)==-EIO);
 assert(nic.poisoned && !nas_netdev_link_up(dev));
 assert(nas_netdev_transmit(dev,frame,sizeof(frame))==-EIO);
 /* Begin removal refuses new pins and packets, but retains DMA until the
  * already-pinned consumer has finished detaching on its own core. */
 assert(nas_uboot_net_detach(&nic)==-EBUSY);
 assert(nas_netdev_is_removing(dev));
 assert(nas_netdev_get("eth-uboot")==NULL);
 assert(nas_netdev_transmit(dev,frame,sizeof(frame))==-ENODEV);
 assert(nas_netdev_poll(dev,4,receive,&calls)==-ENODEV);
 assert(nas_netdev_put(dev)==0);
 assert(nas_uboot_net_detach(&nic)==0);
 all_pages_freed();
 /* Stalled TX may never make upstream's infinite loop a guest hang. */
 struct nas_uboot_net stalled={0};
 struct fake_nic dead={.backend=&stalled,
     .features=NAS_VIRTIO_F_VERSION_1|(UINT64_C(1)<<5),
     .mac={0x52,0x54,0,0x12,0x34,0x56},.stall_tx=true};
 tr.context=&dead;
 assert(nas_uboot_net_attach(&stalled,&tr,"eth-stalled")==0);
 assert(nas_netdev_transmit(&stalled.netdev,frame,sizeof(frame))==-EIO);
 assert(stalled.poisoned && dead.resets>=1);
 assert(nas_uboot_net_detach(&stalled)==0);
 all_pages_freed();
 /* Devices without a ROM MAC have no safe Ethernet identity. */
 struct nas_uboot_net missing={0};
 struct fake_nic no_mac={.backend=&missing,.features=NAS_VIRTIO_F_VERSION_1};
 tr.context=&no_mac;
 assert(nas_uboot_net_attach(&missing,&tr,"eth-no-mac")<0);
 all_pages_freed();
    /* Real class binding supports two simultaneous independently owned PCI
     * NICs, then MMIO reuse. Removing one cannot free the other's DMA. */
    assert(nas_virtio_net_driver_register()==0);
    struct fake_nic discovered={
        .features=NAS_VIRTIO_F_VERSION_1|(UINT64_C(1)<<5),
        .mac={0x52,0x54,0,0x12,0x34,0x56}
    };
    tr.context=&discovered;
    struct nas_bus_device bus={.bus=NAS_BUS_PCI,
        .device_id=VIRTIO_ID_NET,.context=&tr,.resource=&discovered};
    assert(nas_bus_device_register(&bus)==0);
    assert(nas_virtio_net_count()==1);
    dev=nas_netdev_get("eth0");
    assert(dev);
    discovered.backend=dev->backend;
    struct fake_nic discovered2={
        .features=NAS_VIRTIO_F_VERSION_1|(UINT64_C(1)<<5),
        .mac={0x52,0x54,0,0x12,0x34,0x57}
    };
    struct nas_virtio_transport tr2=tr;
    tr2.context=&discovered2;
    struct nas_bus_device bus2={.bus=NAS_BUS_PCI,
        .device_id=VIRTIO_ID_NET,.context=&tr2,.resource=&discovered2};
    assert(nas_bus_device_register(&bus2)==0);
    assert(nas_virtio_net_count()==2);
    struct nas_netdev *dev2=nas_netdev_get("eth1");
    assert(dev2 && dev2!=dev);
    discovered2.backend=dev2->backend;
    assert(nas_netdev_transmit(dev2,frame,sizeof(frame))==0);
    assert(discovered2.tx_count==1);
    assert(nas_netdev_transmit(dev,frame,sizeof(frame))==0);
    assert(discovered.tx_count==1);
    assert(nas_bus_device_unbind(&bus)==-EBUSY);
    assert(nas_netdev_is_removing(dev));
    assert(nas_netdev_get("eth0")==NULL);
    assert(nas_netdev_transmit(dev,frame,sizeof(frame))==-ENODEV);
    assert(nas_netdev_put(dev)==0);
    assert(nas_bus_device_unbind(&bus)==0);
    assert(nas_bus_device_unregister(&bus)==0);
    assert(nas_virtio_net_count()==1);
    assert(nas_netdev_transmit(dev2,frame,sizeof(frame))==0);
    assert(discovered2.tx_count==2);
    assert(nas_bus_device_unbind(&bus2)==-EBUSY);
    assert(nas_netdev_is_removing(dev2));
    assert(nas_netdev_put(dev2)==0);
    assert(nas_bus_device_unbind(&bus2)==0);
    assert(nas_bus_device_unregister(&bus2)==0);
    assert(nas_virtio_net_count()==0);
    all_pages_freed();
    /* Reattach same upstream class driver on VirtIO-MMIO. */
    struct nas_bus_device mmio={.bus=NAS_BUS_VIRTIO_MMIO,
        .device_id=VIRTIO_ID_NET,.context=&tr,.resource=&discovered};
    assert(nas_bus_device_register(&mmio)==0);
    assert(nas_virtio_net_count()==1);
    assert(nas_bus_device_unbind(&mmio)==0);
    assert(nas_bus_device_unregister(&mmio)==0);
    all_pages_freed();
 puts("[ORIGINAL U-BOOT VIRTIO NET PORT PASS] two simultaneous PCI NICs, independent DMA/TX, original eth_ops, RX rearm, malformed, timeout, two-phase hot-unbind, MMIO reuse");
}
