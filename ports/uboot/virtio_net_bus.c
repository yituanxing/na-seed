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
 return dev->device_id==VIRTIO_ID_NET && dev->context!=NULL;
}
static int probe(struct nas_bus_device *dev){
 const struct nas_virtio_transport *tr=dev->context;
 unsigned slot=MAX_NICS;
 for(unsigned i=0;i<MAX_NICS;i++){
     if(nics[i].transport==tr)return -EEXIST;
     if(!nics[i].transport && slot==MAX_NICS)slot=i;
 }
 if(slot==MAX_NICS)return -ENOSPC;
 int rc=nas_uboot_net_attach(&nics[slot].device,tr,names[slot]);
 if(rc)return rc;
 nics[slot].transport=tr;nic_count++;
 return 0;
}
static int remove_device(struct nas_bus_device *dev){
 for(unsigned i=0;i<MAX_NICS;i++)if(nics[i].transport==dev->context){
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
int nas_virtio_net_driver_register(void){
 return nas_bus_driver_register(&driver);
}
