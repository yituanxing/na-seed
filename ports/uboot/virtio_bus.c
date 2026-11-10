/* One bus binding; exactly one original upstream U-Boot C block driver.
 * CPU/device discovery is outside this translation unit. */
#include "virtio_bus.h"
#include "blk_port.h"
#ifdef NAS_ENABLE_VIRTIO_BLK_ASYNC
#include "async_blk.h"
#endif
#include "../../kernel/drivers/core/bus.h"
#include "../../kernel/errno.h"
#include "../../kernel/lib/string.h"
#include <virtio/virtio_ids.h>
#define MAX_BLOCKS 4U
struct slot{struct nas_uboot_block device;const struct nas_virtio_transport *tr;};
static struct slot disks[MAX_BLOCKS];
static const char *const names[MAX_BLOCKS]={"virtio0","virtio1","virtio2","virtio3"};
static unsigned count;
static bool match(const struct nas_bus_device *dev){
#ifdef NAS_ENABLE_VIRTIO_BLK_ASYNC
 /* The separate 16MiB acceptance disk belongs to the asynchronous class.
  * Device order is not stable across PCI and VirtIO-MMIO enumeration. */
 if(nas_async_blk_is_fixture(dev))return false;
#endif
 return dev->device_id==VIRTIO_ID_BLOCK && dev->virtio!=NULL;
}
static int probe(struct nas_bus_device *dev){
 const struct nas_virtio_transport *tr=dev->virtio;
 unsigned index=MAX_BLOCKS;
 for(unsigned i=0;i<MAX_BLOCKS;i++){
     if(disks[i].tr==tr)return -EEXIST;
     if(disks[i].tr)continue;
     /* A failed probe can leave a queue/page owned when the physical
      * release itself fails. Never overwrite that stable owner with a new
      * transport: first retry its original fail-closed cleanup. */
     if(disks[i].device.teardown_pending) {
         int cleanup=nas_uboot_block_detach(&disks[i].device);
         if(cleanup)continue;
         memset(&disks[i].device,0,sizeof(disks[i].device));
     }
     if(index==MAX_BLOCKS)index=i;
 }
 if(index==MAX_BLOCKS)return -ENOSPC;
 int rc=nas_uboot_block_attach(&disks[index].device,tr,names[index]);
 if(rc)return rc;
 disks[index].tr=tr;count++;return 0;
}
static int remove_device(struct nas_bus_device *dev){
 for(unsigned i=0;i<MAX_BLOCKS;i++)if(disks[i].tr==dev->virtio){
    int rc=nas_uboot_block_detach(&disks[i].device);
    if(rc)return rc;
    memset(&disks[i],0,sizeof(disks[i]));count--;return 0;
 }
 return -ENODEV;
}
static const struct nas_bus_driver drv={
 .name="uboot-original-virtio-blk",.bus_mask=NAS_BUS_MASK_ALL,
 .match=match,.probe=probe,.remove=remove_device
};
unsigned nas_virtio_blk_count(void){return count;}
int nas_virtio_blk_driver_register(void){
 return nas_bus_driver_register(&drv);
}
