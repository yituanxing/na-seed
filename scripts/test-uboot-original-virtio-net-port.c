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
/* The production net-port reports a one-shot DMA/IRQ fault through the
 * kernel's serial diagnostic interface. The host fixture supplies that
 * boundary just as it supplies heap, timer and IRQ implementations. */
void nas_print(const char *message){fputs(message,stderr);}
void nas_dec(uint64_t value){fprintf(stderr,"%llu",(unsigned long long)value);}
#define PAGES 80U /* two simultaneously live original 33-page U-Boot NICs */
static _Alignas(4096) uint8_t pages[PAGES][NAS_DMA_PAGE_BYTES];
static bool used[PAGES];
static unsigned route_enables,route_disables;
int nas_arch_irq_route_enable(unsigned hwirq){
 assert(hwirq==11U);route_enables++;return 0;
}
void nas_arch_irq_route_disable(unsigned hwirq){
 assert(hwirq==11U);route_disables++;
}
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
static bool fail_heap_free_once;
bool nas_heap_free(void *p){
    if(fail_heap_free_once){fail_heap_free_once=false;return false;}
    free(p);return true;
}

/* Retryable DMA release failure on one exact call. The original provider
 * must see every successfully released frame once and only once. */
static unsigned release_calls,fail_release_at,fail_release_second;
static int flaky_release(void *context,struct nas_dma_page *page){
    (void)context;
    ++release_calls;
    if(release_calls==fail_release_at){fail_release_at=0;return -EAGAIN;}
    if(release_calls==fail_release_second){fail_release_second=0;return -EAGAIN;}
    return nas_dma_qemu_coherent.release_page(NULL,page);
}

struct fake_nic {
 struct nas_uboot_net *backend;
 uint64_t features;
 uint8_t mac[6];
 uint32_t status;
 unsigned resets,rx_notify,tx_notify,tx_count,rx_cursor;
 uint32_t pending_isr;
 unsigned isr_reads;
 bool stall_tx;
 bool fail_queue_setup;
 bool probe_rx_busy_during_tx;
 unsigned rx_busy_collisions;
 uint8_t tx_frame[1518];
 unsigned tx_length;
};
static void inject(struct fake_nic *,uint32_t written);
static int unexpected_rx(void *ctx,const uint8_t *data,uint32_t bytes){
 (void)ctx;(void)data;(void)bytes;
 assert(!"RX may not drain the queue while TX owns it");
 return -EIO;
}
static struct fake_nic *park_device;
static bool simulate_worker;
static uint64_t monotonic_ns;
uint64_t nas_arch_monotonic_ns(void){return monotonic_ns+=1000000U;}
bool nas_kthread_in_worker(void){return simulate_worker;}
static void worker_woken(struct nas_wait_entry *entry,void *context){
 (void)entry;*(bool *)context=true;
}
enum nas_kwait_result nas_kthread_park_until(struct nas_wait_queue *queue,
                                             uint64_t deadline){
 assert(simulate_worker && park_device && deadline>monotonic_ns);
 if(park_device->stall_tx)return NAS_KWAIT_TIMED_OUT;
 struct nas_wait_entry entry={0};bool woke=false;
 assert(nas_wait_queue_enqueue(queue,&entry,worker_woken,&woke));
 /* Real delivery may interrupt only AFTER this worker parks. Trigger the
  * production IRQ core and wake queue; never complete by polling blindly. */
 park_device->pending_isr=NAS_VIRTIO_IRQ_USED_BUFFER;
 assert(nas_irq_dispatch(11U));
 assert(woke && !entry.queue);
 if(park_device->probe_rx_busy_during_tx){
     /* TX remains suspended with busy=true. Let the RX worker win this
      * scheduling slice after a real IRQ, and inject a completed RX buffer.
      * It must not swallow that sole wake when TX eventually resumes. */
     unsigned attempted=0;
     inject(park_device,12U+60U);
     assert(nas_netdev_poll(&park_device->backend->netdev,4,
                            unexpected_rx,&attempted)==-EBUSY);
     assert(attempted==0);
     ++park_device->rx_busy_collisions;
     park_device->probe_rx_busy_during_tx=false;
 }
 return NAS_KWAIT_READY;
}
static void reset(void *p){
 struct fake_nic *n=p;n->status=0;n->resets++;
}
static uint32_t status(void *p){return ((struct fake_nic *)p)->status;}
static uint32_t read_ack_irq(void *p){
 struct fake_nic *n=p;
 uint32_t source=n->pending_isr;
 n->pending_isr=0;n->isr_reads++;
 return source;
}
static void set_status(void *p,uint32_t s){((struct fake_nic *)p)->status=s;}
static uint32_t features(void *p,unsigned bank){
 return (uint32_t)(((struct fake_nic *)p)->features>>(32*bank));
}
static void write_features(void *p,unsigned bank,uint32_t bits){
 (void)p;(void)bank;(void)bits;
}
static int setup(void *p,unsigned q,unsigned size,uint64_t desc,
                 uint64_t avail,uint64_t complete){
 assert(q<2 && size==(q?8U:NAS_UBOOT_NET_RX_RING_SIZE));
 assert(desc && avail && complete);
 if(q==0 && ((struct fake_nic *)p)->fail_queue_setup)return -EIO;
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
 .setup_queue=setup,.notify_queue=notify,.read_config32=config,
 .read_ack_irq=read_ack_irq
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
static void wake(void *context){
 unsigned *wakes=context;
 (*wakes)++;
}
static int routed_transmit(struct nas_netdev *dev,struct fake_nic *device){
 assert(!simulate_worker && !park_device);
 park_device=device;simulate_worker=true;
 int rc=nas_netdev_transmit(dev,frame,sizeof(frame));
 simulate_worker=false;park_device=NULL;
 return rc;
}
int main(void){
 const struct nas_irq_route route={.domain=NAS_IRQ_DOMAIN_X86_PIC,
                                    .hwirq=11};
 struct nas_uboot_net nic={0};
 struct fake_nic fake={.backend=&nic,
   .features=NAS_VIRTIO_F_VERSION_1|(UINT64_C(1)<<5),
   .mac={0x52,0x54,0,0x12,0x34,0x56}};
 struct nas_virtio_transport tr={.context=&fake,.ops=&ops,
                                 .dma=&nas_dma_qemu_coherent};
 fake.pending_isr=NAS_VIRTIO_IRQ_USED_BUFFER;
 assert(nas_uboot_net_attach(&nic,&tr,"eth-uboot",route)==0);
 assert(nic.netdev.rx_irq_driven && nic.rx_irq.bound);
 assert(!fake.pending_isr && fake.isr_reads>0 && route_enables==1);
 assert(nic.rx_submitted==32 && fake.rx_notify==32);
 assert(!memcmp(nic.netdev.mac,fake.mac,6));
 struct nas_netdev *dev=nas_netdev_get("eth-uboot");
 assert(dev==&nic.netdev && nas_netdev_link_up(dev));
 unsigned wakes=0;
 assert(nas_netdev_set_rx_wake(dev,wake,&wakes)==0);
 fake.pending_isr=NAS_VIRTIO_IRQ_USED_BUFFER;
 assert(nas_irq_dispatch(11));
 assert(!fake.pending_isr && wakes==1 && nic.irq_acks==1 &&
        nic.irq_rx_wakes==1 && dev->rx_sequence>=2);
 assert(!nas_irq_dispatch(11));
 assert(routed_transmit(dev,&fake)==0);
 assert(fake.tx_count==1 && fake.tx_length==sizeof(frame));
 assert(!memcmp(fake.tx_frame,frame,sizeof(frame)));
 const uint64_t legacy_polls=nic.tx_legacy_polls;
 assert(routed_transmit(dev,&fake)==0);
 assert(fake.tx_count==2 && nic.tx_worker_waits>=1 &&
        nic.tx_irq_wakes>=1 && nic.tx_legacy_polls==legacy_polls &&
        legacy_polls==0);
 unsigned calls=0;
 assert(nas_netdev_poll(dev,4,receive,&calls)==0);
 inject(&fake,sizeof(frame)+12U);
 assert(nas_netdev_poll(dev,4,receive,&calls)==1 && calls==1);
 assert(nic.rx_completed==1);
 assert(nic.rx_submitted==33);
 assert(nas_netdev_poll(dev,4,receive,&calls)==0);
 /* One hardware IRQ completes TX and RX. RX runs first and collides with
  * the parked TX owner's busy flag; release must produce one extra wake. */
 const unsigned wake_before_collision=wakes;
 const uint64_t ack_before_collision=nic.irq_acks;
 fake.probe_rx_busy_during_tx=true;
 assert(routed_transmit(dev,&fake)==0);
 assert(fake.rx_busy_collisions==1 && !nic.rx_retry_after_tx);
 assert(nic.irq_acks==ack_before_collision+1U &&
        wakes==wake_before_collision+2U);
 assert(nas_netdev_poll(dev,4,receive,&calls)==1 && calls==2);
 assert(nic.rx_completed==2 && nic.irq_acks==ack_before_collision+1U);
 /* malformed completion poisons device, never leaks stale packet data. */
 inject(&fake,12);
 assert(nas_netdev_poll(dev,4,receive,&calls)==-EIO);
 assert(nic.poisoned && !nas_netdev_link_up(dev));
 assert(nas_netdev_transmit(dev,frame,sizeof(frame))==-EIO);
 /* Begin removal refuses new pins and packets, but retains DMA until the
  * already-pinned consumer has finished detaching on its own core. */
 assert(nas_uboot_net_detach(&nic)==-EBUSY);
 assert(nic.rx_irq.bound);
 assert(nas_netdev_is_removing(dev));
 assert(nas_netdev_get("eth-uboot")==NULL);
 assert(nas_netdev_transmit(dev,frame,sizeof(frame))==-ENODEV);
 assert(nas_netdev_poll(dev,4,receive,&calls)==-ENODEV);
 assert(nas_netdev_set_rx_wake(dev,NULL,NULL)==0);
 assert(nas_netdev_put(dev)==0);
 assert(nas_uboot_net_detach(&nic)==0);
 assert(!nic.rx_irq.bound && route_disables==1);
 all_pages_freed();
 /* Stalled TX may never make upstream's infinite loop a guest hang. */
 struct nas_uboot_net stalled={0};
 struct fake_nic dead={.backend=&stalled,
     .features=NAS_VIRTIO_F_VERSION_1|(UINT64_C(1)<<5),
     .mac={0x52,0x54,0,0x12,0x34,0x56},.stall_tx=true};
 tr.context=&dead;
 assert(nas_uboot_net_attach(&stalled,&tr,"eth-stalled",
                              (struct nas_irq_route){0})==0);
 assert(!stalled.netdev.rx_irq_driven);
 assert(nas_netdev_transmit(&stalled.netdev,frame,sizeof(frame))==-EIO);
 assert(stalled.poisoned && dead.resets>=1);
 assert(nas_uboot_net_detach(&stalled)==0);
 all_pages_freed();
 /* A transport advertising MAC but returning an all-zero address must be
  * rejected after the IRQ action was installed, with full rollback. */
 struct nas_uboot_net missing={0};
 struct fake_nic no_mac={.backend=&missing,
     .features=NAS_VIRTIO_F_VERSION_1|(UINT64_C(1)<<5)};
 tr.context=&no_mac;
 assert(nas_uboot_net_attach(&missing,&tr,"eth-no-mac",route)<0);
 assert(!missing.rx_irq.bound && route_enables==2 && route_disables==2);
 all_pages_freed();
 /* A resolved hardware IRQ without an ISR reader must fail-closed; never
  * advertise IRQ-driven RX and silently fall back to timer polling. */
 struct nas_uboot_net no_ack={0};
 struct fake_nic no_ack_fake={.backend=&no_ack,
     .features=NAS_VIRTIO_F_VERSION_1|(UINT64_C(1)<<5),
     .mac={0x52,0x54,0,0x12,0x34,0x57}};
 struct nas_virtio_transport_ops no_ack_ops=ops;
 no_ack_ops.read_ack_irq=NULL;
 tr=(struct nas_virtio_transport){.context=&no_ack_fake,
     .ops=&no_ack_ops,.dma=&nas_dma_qemu_coherent};
 assert(nas_uboot_net_attach(&no_ack,&tr,"eth-no-isr",route)==-EOPNOTSUPP);
 assert(!no_ack.rx_irq.bound && route_enables==2);
 all_pages_freed();
    /* A class-device withdrawal succeeds before the first vring DMA page
     * release is rejected. The second detach must NOT retry unregister on
     * a vanished netdev, must retain ownership, and must finish once. */
    struct nas_dma_provider flaky_dma=nas_dma_qemu_coherent;
    flaky_dma.release_page=flaky_release;
    struct nas_uboot_net partial={0};
    struct fake_nic partial_fake={.backend=&partial,
        .features=NAS_VIRTIO_F_VERSION_1|(UINT64_C(1)<<5),
        .mac={0x52,0x54,0,0x12,0x34,0x68}};
    tr=(struct nas_virtio_transport){.context=&partial_fake,
        .ops=&ops,.dma=&flaky_dma};
    assert(nas_uboot_net_attach(&partial,&tr,"eth-partial",route)==0);
    unsigned disabled_before=route_disables;
    release_calls=0;fail_release_at=1;
    assert(nas_uboot_net_detach(&partial)==-EAGAIN);
    assert(partial.teardown_active && partial.class_retired &&
           !partial.netdev.registered && !partial.rx_ring.active &&
           partial.rx_ring.dma_page.cpu &&
           route_disables==disabled_before+1U);
    assert(nas_uboot_net_attach(&partial,&tr,"eth-overwrite",route)==-EBUSY);
    assert(nas_uboot_net_detach(&partial)==0);
    assert(!partial.teardown_active && !partial.rx_ring.active &&
           !partial.tx_ring.active && release_calls==36U &&
           route_disables==disabled_before+1U);
    all_pages_freed();
    /* Both hardware queues can retire successfully while a payload page
     * remains DMA-owned. Retry must skip already-freed ring pages. */
    struct nas_uboot_net partial_dma={0};
    struct fake_nic partial_dma_fake={.backend=&partial_dma,
        .features=NAS_VIRTIO_F_VERSION_1|(UINT64_C(1)<<5),
        .mac={0x52,0x54,0,0x12,0x34,0x69}};
    tr.context=&partial_dma_fake;
    assert(nas_uboot_net_attach(&partial_dma,&tr,"eth-dma-retry",route)==0);
    release_calls=0;fail_release_at=3;
    assert(nas_uboot_net_detach(&partial_dma)==-EAGAIN);
    assert(partial_dma.teardown_active && partial_dma.class_retired &&
           !partial_dma.rx_ring.active && !partial_dma.tx_ring.active &&
           partial_dma.rx_dma_count==NAS_UBOOT_NET_RX_SLOTS &&
           partial_dma.rx_dma[0].cpu);
    assert(nas_uboot_net_detach(&partial_dma)==0 && release_calls==36U);
    all_pages_freed();
    /* Terminal heap-provider error is not silently converted into another
     * scheduled bus retry, but explicit maintenance can resume the exact
     * remaining heap stage without touching already retired DMA again. */
    struct nas_uboot_net partial_heap={0};
    struct fake_nic partial_heap_fake={.backend=&partial_heap,
        .features=NAS_VIRTIO_F_VERSION_1|(UINT64_C(1)<<5),
        .mac={0x52,0x54,0,0x12,0x34,0x6a}};
    tr.context=&partial_heap_fake;
    tr.dma=&nas_dma_qemu_coherent;
    assert(nas_uboot_net_attach(&partial_heap,&tr,"eth-heap-retry",route)==0);
    fail_heap_free_once=true;
    assert(nas_uboot_net_detach(&partial_heap)==-EIO);
    assert(partial_heap.teardown_active && partial_heap.class_retired &&
           partial_heap.udev.priv && !partial_heap.rx_ring.active &&
           !partial_heap.tx_ring.active && !partial_heap.tx_dma.cpu &&
           partial_heap.rx_dma_count==0);
    all_pages_freed();
    assert(nas_uboot_net_detach(&partial_heap)==0 && !partial_heap.udev.priv);
    all_pages_freed();
    /* The same recovery contract applies to a probe failure after DMA and
     * IRQ setup but BEFORE the class device was ever published. */
    struct nas_uboot_net failed_attach={0};
    struct fake_nic failed_attach_fake={.backend=&failed_attach,
        .features=NAS_VIRTIO_F_VERSION_1|(UINT64_C(1)<<5)};
    tr.context=&failed_attach_fake;
    tr.dma=&flaky_dma;
    release_calls=0;fail_release_at=1;
    assert(nas_uboot_net_attach(&failed_attach,&tr,"eth-no-mac-retry",route)<0);
    assert(failed_attach.teardown_active && failed_attach.class_retired &&
           !failed_attach.netdev.registered && !failed_attach.rx_ring.active &&
           failed_attach.rx_ring.dma_page.cpu);
    assert(nas_uboot_net_attach(&failed_attach,&tr,"eth-overwrite",route)==-EBUSY);
    assert(nas_uboot_net_detach(&failed_attach)==0);
    all_pages_freed();
    /* Failed vring INIT has an inactive ring but may still own a DMA page
     * if its own rollback release fails. A second failure during driver
     * cleanup retains this invisible owner until the next detach. */
    struct nas_uboot_net inactive_ring={0};
    struct fake_nic incomplete_queue={.backend=&inactive_ring,
        .features=NAS_VIRTIO_F_VERSION_1|(UINT64_C(1)<<5),
        .mac={0x52,0x54,0,0x12,0x34,0x6b},.fail_queue_setup=true};
    tr.context=&incomplete_queue;
    tr.dma=&flaky_dma;
    release_calls=0;fail_release_at=1;fail_release_second=2;
    assert(nas_uboot_net_attach(&inactive_ring,&tr,
                                "eth-inactive-ring",route)<0);
    assert(inactive_ring.teardown_active && inactive_ring.class_retired &&
           !inactive_ring.rx_ring.active && inactive_ring.rx_ring.dma_page.cpu);
    assert(nas_uboot_net_attach(&inactive_ring,&tr,"eth-overwrite",route)==-EBUSY);
    assert(nas_uboot_net_detach(&inactive_ring)==0);
    all_pages_freed();
    /* Start the independent multi-NIC bus fixture with its original IRQ
     * enable/disable baseline; no prior test has a live action. */
    route_enables=2;
    route_disables=2;
    /* Real class binding supports two simultaneous independently owned PCI
     * NICs, then MMIO reuse. Removing one cannot free the other's DMA. */
    assert(nas_virtio_net_driver_register()==0);
    struct fake_nic discovered={
        .features=NAS_VIRTIO_F_VERSION_1|(UINT64_C(1)<<5),
        .mac={0x52,0x54,0,0x12,0x34,0x56}
    };
    tr.context=&discovered;tr.ops=&ops;
    static uint8_t pci_config[NAS_BUS_PCI_CONFIG_BYTES];
    static volatile uint32_t mmio_registers[128];
    struct nas_bus_device bus={.bus=NAS_BUS_PCI,
        .device_id=VIRTIO_ID_NET,.virtio=&tr,
        .resource={.kind=NAS_BUS_RESOURCE_PCI,
                   .irq={.domain=NAS_IRQ_DOMAIN_X86_PIC,.hwirq=11},
                   .view.pci={.config=pci_config,
                              .config_bytes=NAS_BUS_PCI_CONFIG_BYTES,
                              .mapped_bar_mask=1}}};
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
        .device_id=VIRTIO_ID_NET,.virtio=&tr2,
        .resource={.kind=NAS_BUS_RESOURCE_PCI,
                   .irq={.domain=NAS_IRQ_DOMAIN_X86_PIC,.hwirq=11},
                   .view.pci={.config=pci_config,
                              .config_bytes=NAS_BUS_PCI_CONFIG_BYTES,
                              .slot=1,.mapped_bar_mask=1}}};
    assert(nas_bus_device_register(&bus2)==0);
    assert(nas_virtio_net_count()==2);
    struct nas_netdev *dev2=nas_netdev_get("eth1");
    assert(dev2 && dev2!=dev);
    discovered2.backend=dev2->backend;
    /* Two independently owned class actions share one physical IRQ11.
     * Both device-specific ISR sources must be ACKed before controller EOI;
     * no second controller enable and no duplicate ACK for a single NIC. */
    assert(route_enables==3 && route_disables==2);
    assert(dev->rx_irq_driven && dev2->rx_irq_driven);
    unsigned wake0=0,wake1=0;
    assert(nas_netdev_set_rx_wake(dev,wake,&wake0)==0);
    assert(nas_netdev_set_rx_wake(dev2,wake,&wake1)==0);
    discovered.pending_isr=NAS_VIRTIO_IRQ_USED_BUFFER;
    discovered2.pending_isr=NAS_VIRTIO_IRQ_USED_BUFFER;
    assert(nas_irq_dispatch(11));
    assert(!discovered.pending_isr && !discovered2.pending_isr);
    assert(wake0==1 && wake1==1);
    struct nas_virtio_net_irq_stats stats={0};
    nas_virtio_net_irq_totals(&stats);
    assert(stats.irq_driven_nics==2 && stats.irq_acks==2 &&
           stats.irq_rx_wakes==2);
    assert(routed_transmit(dev2,&discovered2)==0);
    assert(discovered2.tx_count==1);
    assert(routed_transmit(dev,&discovered)==0);
    assert(discovered.tx_count==1);
    assert(nas_bus_device_request_remove(&bus)==0);
    assert(nas_bus_device_request_remove(&bus)==0);
    assert(nas_bus_device_remove_status(&bus)==-EINPROGRESS);
    assert(nas_netdev_is_removing(dev));
    assert(nas_netdev_get("eth0")==NULL);
    assert(nas_netdev_transmit(dev,frame,sizeof(frame))==-ENODEV);
    assert(nas_bus_service_removals(1)==1 && bus.registered);
    assert(nas_netdev_set_rx_wake(dev,NULL,NULL)==0);
    assert(nas_netdev_put(dev)==0);
    assert(nas_bus_service_removals(1)==1);
    assert(nas_bus_device_remove_status(&bus)==0 && !bus.registered);
    assert(nas_virtio_net_count()==1);
    assert(route_disables==2); /* shared second owner keeps IRQ11 live */
    nas_virtio_net_irq_totals(&stats);
    /* The surviving NIC also received its TX completion IRQ above. */
    assert(stats.irq_driven_nics==1 && stats.irq_acks==2 &&
           stats.irq_acks==discovered2.backend->irq_acks);
    assert(routed_transmit(dev2,&discovered2)==0);
    assert(discovered2.tx_count==2);
    assert(nas_bus_device_request_remove(&bus2)==0);
    assert(nas_bus_device_remove_status(&bus2)==-EINPROGRESS);
    assert(nas_netdev_is_removing(dev2));
    assert(nas_netdev_set_rx_wake(dev2,NULL,NULL)==0);
    assert(nas_netdev_put(dev2)==0);
    assert(nas_bus_service_removals(1)==1);
    assert(nas_bus_device_remove_status(&bus2)==0 && !bus2.registered);
    assert(nas_virtio_net_count()==0);
    assert(route_disables==3 && !nas_irq_dispatch(11));
    all_pages_freed();
    /* Reattach same upstream class driver on VirtIO-MMIO. */
    struct nas_bus_device mmio={.bus=NAS_BUS_VIRTIO_MMIO,
        .device_id=VIRTIO_ID_NET,.virtio=&tr,
        .resource={.kind=NAS_BUS_RESOURCE_VIRTIO_MMIO,
                   .view.mmio={mmio_registers,sizeof(mmio_registers)}}};
    assert(nas_bus_device_register(&mmio)==0);
    assert(nas_virtio_net_count()==1);
    assert(nas_bus_device_unbind(&mmio)==0);
    assert(nas_bus_device_unregister(&mmio)==0);
    all_pages_freed();
    /* A failed bus probe does not acquire a BusDriver binding. Its static
     * slot nonetheless retains the failed DMA release. The NEXT bus probe
     * must retry that cleanup before reusing the same class-driver slot,
     * never memset the stale transport or leak a 35-page owner. */
    struct fake_nic stale={
        .features=NAS_VIRTIO_F_VERSION_1|(UINT64_C(1)<<5)
    };
    struct nas_virtio_transport stale_tr={.context=&stale,.ops=&ops,
                                         .dma=&flaky_dma};
    struct nas_bus_device stale_bus={.bus=NAS_BUS_PCI,
        .device_id=VIRTIO_ID_NET,.virtio=&stale_tr,
        .resource={.kind=NAS_BUS_RESOURCE_PCI,
                   .irq={.domain=NAS_IRQ_DOMAIN_X86_PIC,.hwirq=11},
                   .view.pci={.config=pci_config,
                              .config_bytes=NAS_BUS_PCI_CONFIG_BYTES,
                              .slot=2,.mapped_bar_mask=1}}};
    release_calls=0;fail_release_at=1;
    assert(nas_bus_device_register(&stale_bus)<0 && !stale_bus.registered);
    assert(nas_virtio_net_count()==0 && release_calls==1);
    memcpy(stale.mac,discovered.mac,sizeof(stale.mac));
    assert(nas_bus_device_register(&stale_bus)==0);
    assert(nas_virtio_net_count()==1);
    struct nas_netdev *recovered=nas_netdev_get("eth0");
    if(recovered)stale.backend=recovered->backend;
    assert(recovered && nas_netdev_put(recovered)==0);
    /* The real bus owns the retry, not an ad-hoc driver polling loop. */
    release_calls=0;fail_release_at=1;
    assert(nas_bus_device_request_remove(&stale_bus)==0);
    assert(nas_bus_device_remove_status(&stale_bus)==-EINPROGRESS &&
           !stale.backend->netdev.registered);
    assert(nas_bus_service_removals(1)==1);
    assert(nas_bus_device_remove_status(&stale_bus)==0 &&
           !stale_bus.registered && nas_virtio_net_count()==0);
    assert(release_calls==36U);
    all_pages_freed();
    /* A routed NIC's stalled TX parks its kernel C frame and times out,
     * retaining the DMA buffer until the poison/reset path has run. */
    struct nas_uboot_net timed={0};
    struct fake_nic hung={.backend=&timed,
        .features=NAS_VIRTIO_F_VERSION_1|(UINT64_C(1)<<5),
        .mac={0x52,0x54,0,0x12,0x34,0x59},.stall_tx=true};
    tr.context=&hung;tr.ops=&ops;
    assert(nas_uboot_net_attach(&timed,&tr,"eth-timeout",route)==0);
    assert(routed_transmit(&timed.netdev,&hung)==-EIO);
    assert(timed.poisoned && timed.tx_worker_waits==1U &&
           timed.tx_legacy_polls==0 && hung.resets>=1U);
    assert(nas_uboot_net_detach(&timed)==0);
    all_pages_freed();
    /* A routed NIC must never silently poll if called outside a worker;
     * non-routed early clients retain their bounded legacy compatibility. */
    struct nas_uboot_net wrong={0};
    struct fake_nic wrong_device={.backend=&wrong,
        .features=NAS_VIRTIO_F_VERSION_1|(UINT64_C(1)<<5),
        .mac={0x52,0x54,0,0x12,0x34,0x5a}};
    tr.context=&wrong_device;
    assert(nas_uboot_net_attach(&wrong,&tr,"eth-no-worker",route)==0);
    assert(nas_netdev_transmit(&wrong.netdev,frame,sizeof(frame))==-EIO);
    assert(wrong.poisoned && wrong.tx_legacy_polls==0 &&
           wrong_device.resets>=1U);
    assert(nas_uboot_net_detach(&wrong)==0);
    all_pages_freed();
    struct nas_uboot_net early={0};
    struct fake_nic early_device={.backend=&early,
        .features=NAS_VIRTIO_F_VERSION_1|(UINT64_C(1)<<5),
        .mac={0x52,0x54,0,0x12,0x34,0x5b}};
    tr.context=&early_device;
    assert(nas_uboot_net_attach(&early,&tr,"eth-no-irq",
                                (struct nas_irq_route){0})==0);
    assert(nas_netdev_transmit(&early.netdev,frame,sizeof(frame))==0);
    assert(early.tx_legacy_polls>0 && !early.tx_worker_waits);
    assert(nas_uboot_net_detach(&early)==0);
    all_pages_freed();
 puts("[ORIGINAL U-BOOT VIRTIO NET PORT PASS] two PCI NICs, IRQ-only worker TX, RX-busy deferred retry with one wake, timeout and no-worker fail-closed, non-IRQ fallback, DMA, RX, hot-unbind, MMIO reuse");
}
