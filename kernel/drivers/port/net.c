#include "net.h"
#include "../../errno.h"
#include "../../lib/string.h"
#include <stddef.h>
#include <stdint.h>

/* The sole Ethernet device registry, independent of lwIP and any particular
 * NIC. The bootstrap kernel is single-core; the future preemptible/network
 * worker phase must protect registry operations with the same OS lock. */
static struct nas_netdev *devices[NAS_NETDEV_LIMIT];

int nas_netdev_register(struct nas_netdev *device) {
    if(!device || !device->name || !device->name[0] || !device->backend ||
       !device->ops || !device->ops->transmit ||
       !device->ops->poll || !device->ops->link_up ||
       device->mtu<576U || device->mtu>9000U ||
       device->registered || device->refs || device->rx_wake ||
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
    return 0;
}
int nas_netdev_unregister(struct nas_netdev *device) {
    if(!device || !device->registered)return -ENODEV;
    if(device->refs)return -EBUSY;
    for(unsigned i=0;i<NAS_NETDEV_LIMIT;i++)if(devices[i]==device){
        devices[i]=NULL;
        device->registered=false;
        return 0;
    }
    return -ENODEV;
}
static struct nas_netdev *pin_device(struct nas_netdev *device) {
    if(!device || !device->registered || device->refs==UINT32_MAX)return NULL;
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
        if(!device)continue;
        if(!ordinal)return pin_device(device);
        ordinal--;
    }
    return NULL;
}
int nas_netdev_put(struct nas_netdev *device) {
    if(!device || !device->registered || !device->refs)return -EINVAL;
    /* The last consumer must detach its IRQ wake before releasing the pin;
     * otherwise an interrupt could target a retired network worker. */
    if(device->refs==1 && device->rx_wake)return -EBUSY;
    device->refs--;
    return 0;
}
int nas_netdev_transmit(struct nas_netdev *device,
                        const uint8_t *frame,uint32_t bytes) {
    if(!device || !device->registered)return -ENODEV;
    if(!frame || bytes<14U || bytes>(uint32_t)device->mtu+18U)
        return -EMSGSIZE;
    return device->ops->transmit(device->backend,frame,bytes);
}
int nas_netdev_poll(struct nas_netdev *device,uint32_t budget,
                   nas_netdev_rx receive,void *consumer) {
    if(!device || !device->registered)return -ENODEV;
    if(!budget || !receive)return -EINVAL;
    int rc=device->ops->poll(device->backend,budget,receive,consumer);
    return rc>=0 && (uint32_t)rc>budget ? -EIO : rc;
}
int nas_netdev_set_rx_wake(struct nas_netdev *device,
                           nas_netdev_wake wake,void *consumer) {
    if(!device || !device->registered)return -ENODEV;
    if(!device->refs || (wake && !consumer))return -EINVAL;
    if(wake && device->rx_wake)return -EBUSY;
    device->rx_wake=wake;
    device->rx_wake_context=wake?consumer:NULL;
    return 0;
}
void nas_netdev_notify_rx(struct nas_netdev *device) {
    if(!device || !device->registered)return;
    ++device->rx_sequence;
    /* The callback may enqueue a worker/semaphore wake. It must not sleep,
     * dispatch a packet or unbind this live device from IRQ context. */
    if(device->rx_wake)device->rx_wake(device->rx_wake_context);
}
bool nas_netdev_link_up(const struct nas_netdev *device) {
    return device && device->registered &&
           device->ops->link_up(device->backend);
}
