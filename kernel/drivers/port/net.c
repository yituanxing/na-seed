#include "net.h"
#include "../../errno.h"
#include "../../lib/string.h"
#include <stddef.h>
#include <stdint.h>

/* The sole Ethernet device registry, independent of lwIP and any particular
 * NIC. The bootstrap kernel is single-core; the future preemptible/network
 * worker phase must protect registry operations with the same OS lock. */
static struct nas_netdev *devices[NAS_NETDEV_LIMIT];
static nas_netdev_wake change_wake;
static void *change_context;
static nas_netdev_packet_tap packet_tap;
static void *packet_tap_context;
/* IRQs can interrupt callback installation on today's single CPU. Publish
 * the non-NULL context before the callback and retire the callback before
 * clearing the context; an acquire snapshot observes that order. This is
 * not a substitute for a registry/teardown lock when SMP is introduced. */
static void invoke_wake(nas_netdev_wake *callback,void **context) {
    nas_netdev_wake wake=__atomic_load_n(callback,__ATOMIC_ACQUIRE);
    if(wake)wake(__atomic_load_n(context,__ATOMIC_RELAXED));
}
static void set_wake(nas_netdev_wake *callback,void **context,
                     nas_netdev_wake wake,void *consumer) {
    if(wake) {
        __atomic_store_n(context,consumer,__ATOMIC_RELAXED);
        __atomic_store_n(callback,wake,__ATOMIC_RELEASE);
    } else {
        /* The two seq-cst stores forbid compiler/CPU reversal during
         * IRQ-visible teardown, including on weakly ordered RV/ARM. */
        __atomic_store_n(callback,NULL,__ATOMIC_SEQ_CST);
        __atomic_store_n(context,NULL,__ATOMIC_SEQ_CST);
    }
}
static void notify_change(void) {
    invoke_wake(&change_wake,&change_context);
}
int nas_netdev_set_change_wake(nas_netdev_wake wake,void *consumer) {
    if(wake && !consumer)return -EINVAL;
    if(wake && __atomic_load_n(&change_wake,__ATOMIC_ACQUIRE))return -EBUSY;
    set_wake(&change_wake,&change_context,wake,consumer);
    return 0;
}
int nas_netdev_set_packet_tap(nas_netdev_packet_tap tap,void *consumer) {
    if(tap && (!consumer || packet_tap))return -EBUSY;
    /* This setter and the worker-context producer run on the same
     * non-preemptive CPU. The IRQ path never calls packet_tap. */
    packet_tap_context=tap?consumer:NULL;
    packet_tap=tap;
    return 0;
}
static void packet_observe(const struct nas_netdev *device,
                           const uint8_t *frame,uint32_t bytes,bool tx) {
    if(packet_tap)packet_tap(packet_tap_context,device,frame,bytes,tx);
}

int nas_netdev_register(struct nas_netdev *device) {
    if(!device || !device->name || !device->name[0] || !device->backend ||
       !device->ops || !device->ops->transmit ||
       !device->ops->poll || !device->ops->link_up ||
       device->mtu<576U || device->mtu>9000U ||
       device->registered || device->removing || device->refs || device->rx_wake ||
       device->rx_wake_context)return -EINVAL;
    /* Only Ethernet unicast MACs can back a normal lwIP netif. */
    unsigned mac_nonzero=0;
    for(unsigned b=0;b<NAS_NETDEV_MAC_BYTES;b++)mac_nonzero|=device->mac[b];
    if(!mac_nonzero || (device->mac[0]&1U))return -EINVAL;
    unsigned free_slot=NAS_NETDEV_LIMIT;
    for(unsigned i=0;i<NAS_NETDEV_LIMIT;i++){
        if(devices[i] && !strcmp(devices[i]->name,device->name))return -EEXIST;
        if(!devices[i] && free_slot==NAS_NETDEV_LIMIT)free_slot=i;
    }
    if(free_slot==NAS_NETDEV_LIMIT)return -ENOSPC;
    devices[free_slot]=device;
    device->registered=true;
    notify_change();
    return 0;
}
int nas_netdev_begin_remove(struct nas_netdev *device) {
    if(!device || !device->registered)return -ENODEV;
    if(device->removing)return 0;
    device->removing=true;
    /* A final pin may belong to the lwIP worker. It must observe removal
     * and schedule netif_detach on tcpip_thread before the driver can free
     * transport rings or DMA. Never invoke the core from this callback. */
    invoke_wake(&device->rx_wake,&device->rx_wake_context);
    notify_change();
    return 0;
}
bool nas_netdev_is_removing(const struct nas_netdev *device) {
    return device && device->registered && device->removing;
}
int nas_netdev_unregister(struct nas_netdev *device) {
    if(!device || !device->registered)return -ENODEV;
    if(device->refs)return -EBUSY;
    for(unsigned i=0;i<NAS_NETDEV_LIMIT;i++)if(devices[i]==device){
        devices[i]=NULL;
        device->registered=false;
        device->removing=false;
        notify_change();
        return 0;
    }
    return -ENODEV;
}
static struct nas_netdev *pin_device(struct nas_netdev *device) {
    if(!device || !device->registered || device->removing ||
       device->refs==UINT32_MAX)return NULL;
    device->refs++;
    return device;
}
struct nas_netdev *nas_netdev_get(const char *name) {
    if(!name)return NULL;
    for(unsigned i=0;i<NAS_NETDEV_LIMIT;i++){
        struct nas_netdev *device=devices[i];
        if(device && !strcmp(device->name,name))return pin_device(device);
    }
    return NULL;
}
struct nas_netdev *nas_netdev_get_ordinal(unsigned ordinal) {
    for(unsigned i=0;i<NAS_NETDEV_LIMIT;i++){
        struct nas_netdev *device=devices[i];
        if(!device || device->removing)continue;
        if(!ordinal)return pin_device(device);
        ordinal--;
    }
    return NULL;
}
int nas_netdev_put(struct nas_netdev *device) {
    if(!device || !device->registered || !device->refs)return -EINVAL;
    /* The last consumer must detach its IRQ wake before releasing the pin;
     * otherwise an interrupt could target a retired network worker. */
    if(device->refs==1 &&
       __atomic_load_n(&device->rx_wake,__ATOMIC_ACQUIRE))return -EBUSY;
    device->refs--;
    return 0;
}
int nas_netdev_transmit(struct nas_netdev *device,
                        const uint8_t *frame,uint32_t bytes) {
    if(!device || !device->registered || device->removing)return -ENODEV;
    if(!frame || bytes<14U || bytes>(uint32_t)device->mtu+18U)
        return -EMSGSIZE;
    int rc=device->ops->transmit(device->backend,frame,bytes);
    if(!rc) {
        packet_observe(device,frame,bytes,true);
        /* Driver has copied/queued the actual Ethernet frame. Completion
         * interrupts are not a second packet, and ring backpressure is
         * never reported as a successful transmission. */
        __atomic_fetch_add(&device->stats.tx_packets,1,__ATOMIC_RELAXED);
        __atomic_fetch_add(&device->stats.tx_bytes,bytes,__ATOMIC_RELAXED);
    } else if(rc!=-EAGAIN && rc!=-EBUSY && rc!=-ENOBUFS) {
        __atomic_fetch_add(&device->stats.tx_errors,1,__ATOMIC_RELAXED);
    }
    return rc;
}
struct rx_count_context {
    struct nas_netdev *device;
    nas_netdev_rx callback;
    void *consumer;
};
static int counted_receive(void *context,const uint8_t *frame,uint32_t bytes) {
    struct rx_count_context *observer=context;
    packet_observe(observer->device,frame,bytes,false);
    int rc=observer->callback(observer->consumer,frame,bytes);
    if(!rc) {
        __atomic_fetch_add(&observer->device->stats.rx_packets,1,
                           __ATOMIC_RELAXED);
        __atomic_fetch_add(&observer->device->stats.rx_bytes,bytes,
                           __ATOMIC_RELAXED);
    } else {
        /* The Ethernet device did deliver a frame, but its consumer
         * rejected it; never count the frame as received by the stack. */
        __atomic_fetch_add(&observer->device->stats.rx_dropped,1,
                           __ATOMIC_RELAXED);
    }
    return rc;
}
int nas_netdev_poll(struct nas_netdev *device,uint32_t budget,
                   nas_netdev_rx receive,void *consumer) {
    if(!device || !device->registered || device->removing)return -ENODEV;
    if(!budget || !receive)return -EINVAL;
    struct rx_count_context observer={device,receive,consumer};
    int rc=device->ops->poll(device->backend,budget,counted_receive,&observer);
    return rc>=0 && (uint32_t)rc>budget ? -EIO : rc;
}
int nas_netdev_set_rx_wake(struct nas_netdev *device,
                           nas_netdev_wake wake,void *consumer) {
    if(!device || !device->registered)return -ENODEV;
    if(!device->refs || (wake && !consumer))return -EINVAL;
    if(wake && device->removing)return -ENODEV;
    if(wake && __atomic_load_n(&device->rx_wake,__ATOMIC_ACQUIRE))
        return -EBUSY;
    set_wake(&device->rx_wake,&device->rx_wake_context,wake,consumer);
    return 0;
}
void nas_netdev_notify_rx(struct nas_netdev *device) {
    if(!device || !device->registered)return;
    (void)__atomic_fetch_add(&device->rx_sequence,1,__ATOMIC_RELEASE);
    /* The callback may enqueue a worker/semaphore wake. It must not sleep,
     * dispatch a packet or unbind this live device from IRQ context. */
    invoke_wake(&device->rx_wake,&device->rx_wake_context);
}
bool nas_netdev_link_up(const struct nas_netdev *device) {
    return device && device->registered && !device->removing &&
           device->ops->link_up(device->backend);
}
int nas_netdev_stats_snapshot(const struct nas_netdev *device,
                              struct nas_netdev_stats *out) {
    if(!device || !device->registered || !out)return -ENODEV;
    out->rx_bytes=__atomic_load_n(&device->stats.rx_bytes,__ATOMIC_RELAXED);
    out->rx_packets=__atomic_load_n(&device->stats.rx_packets,__ATOMIC_RELAXED);
    out->rx_dropped=__atomic_load_n(&device->stats.rx_dropped,__ATOMIC_RELAXED);
    out->tx_bytes=__atomic_load_n(&device->stats.tx_bytes,__ATOMIC_RELAXED);
    out->tx_packets=__atomic_load_n(&device->stats.tx_packets,__ATOMIC_RELAXED);
    out->tx_errors=__atomic_load_n(&device->stats.tx_errors,__ATOMIC_RELAXED);
    return 0;
}
