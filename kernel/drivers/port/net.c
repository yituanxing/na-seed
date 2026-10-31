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
       !device->ops->poll_receive || !device->ops->link_up ||
       device->mtu<576U || device->mtu>9000U ||
       device->registered || device->refs)return -EINVAL;
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
struct nas_netdev *nas_netdev_get(const char *name) {
    if(!name)return NULL;
    for(unsigned i=0;i<NAS_NETDEV_LIMIT;i++){
        struct nas_netdev *device=devices[i];
        if(device && !strcmp(device->name,name)){
            if(device->refs==UINT32_MAX)return NULL;
            device->refs++;
            return device;
        }
    }
    return NULL;
}
int nas_netdev_put(struct nas_netdev *device) {
    if(!device || !device->registered || !device->refs)return -EINVAL;
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
int nas_netdev_poll_receive(struct nas_netdev *device,uint32_t budget,
                            nas_netdev_rx receive,void *consumer) {
    if(!device || !device->registered)return -ENODEV;
    if(!budget || !receive)return -EINVAL;
    int rc=device->ops->poll_receive(device->backend,budget,receive,consumer);
    return rc>=0 && (uint32_t)rc>budget ? -EIO : rc;
}
bool nas_netdev_link_up(const struct nas_netdev *device) {
    return device && device->registered &&
           device->ops->link_up(device->backend);
}
