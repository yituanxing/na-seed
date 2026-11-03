/* Original U-Boot Ethernet class driver, bound through NaSeed's one bus
 * registry. Transport/queue code is shared with the block class. */
#include "virtio_net_bus.h"
#include "net_port.h"
#include "../../kernel/drivers/core/bus.h"
#include "../../kernel/errno.h"
#include "../../kernel/lib/string.h"
#include <virtio/virtio_ids.h>
#define MAX_NICS 2U
struct nic_slot {
 struct nas_uboot_net device;
 const struct nas_virtio_transport *transport;
};
static struct nic_slot nics[MAX_NICS];
static const char *const names[MAX_NICS]={"eth0","eth1"};
static unsigned nic_count;
static bool match(const struct nas_bus_device *dev){
 return dev->device_id==VIRTIO_ID_NET && dev->virtio!=NULL;
}
static int probe(struct nas_bus_device *dev){
 const struct nas_virtio_transport *tr=dev->virtio;
 unsigned slot=MAX_NICS;
 for(unsigned i=0;i<MAX_NICS;i++){
     if(nics[i].transport==tr)return -EEXIST;
     if(!nics[i].transport && slot==MAX_NICS)slot=i;
 }
 if(slot==MAX_NICS)return -ENOSPC;
 int rc=nas_uboot_net_attach(&nics[slot].device,tr,names[slot],
                             dev->resource.irq);
 if(rc)return rc;
 nics[slot].transport=tr;nic_count++;
 return 0;
}
static int remove_device(struct nas_bus_device *dev){
 for(unsigned i=0;i<MAX_NICS;i++)if(nics[i].transport==dev->virtio){
     int rc=nas_uboot_net_detach(&nics[i].device);
     if(rc)return rc;
     memset(&nics[i],0,sizeof(nics[i]));nic_count--;
     return 0;
 }
 return -ENODEV;
}
static const struct nas_bus_driver driver={
 .name="uboot-original-virtio-net",.bus_mask=NAS_BUS_MASK_ALL,
 .match=match,.probe=probe,.remove=remove_device
};
unsigned nas_virtio_net_count(void){return nic_count;}
void nas_virtio_net_irq_totals(struct nas_virtio_net_irq_stats *stats){
 if(!stats)return;
 *stats=(struct nas_virtio_net_irq_stats){0};
 for(unsigned i=0;i<MAX_NICS;i++){
     const struct nas_uboot_net *n=&nics[i].device;
     if(!nics[i].transport)continue;
     stats->irq_acks+=n->irq_acks;
     stats->irq_rx_wakes+=n->irq_rx_wakes;
     stats->rx_completed+=n->rx_completed;
     stats->tx_irq_wakes+=n->tx_irq_wakes;
     stats->tx_worker_waits+=n->tx_worker_waits;
     stats->tx_legacy_polls+=n->tx_legacy_polls;
     if(n->netdev.rx_irq_driven){
         stats->irq_driven_nics++;
         /* A total ACK count can hide an unserviced second NIC. Count
          * only independently witnessed ISR, worker wake and RX traffic. */
         if(n->irq_acks && n->irq_rx_wakes && n->rx_completed)
             stats->irq_proven_nics++;
         if(n->tx_irq_wakes && n->tx_worker_waits && !n->tx_legacy_polls)
             stats->tx_proven_nics++;
     }
 }
}
int nas_virtio_net_driver_register(void){
 return nas_bus_driver_register(&driver);
}
