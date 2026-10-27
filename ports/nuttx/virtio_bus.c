/* A device bus binds by VirtIO device type, never CPU ISA or FS type.
 * Request protocol logic belongs only to ports/nuttx/virtio_blk.c. */
#include "virtio_bus.h"
#include "virtio_blk.h"
#include "../../kernel/drivers/core/bus.h"
#include "../../kernel/errno.h"
#include "../../kernel/lib/string.h"
#include <virtio/virtio_ids.h>
#include <stdint.h>

#define NAS_NUTTX_MAX_BLOCK 4U
struct disk_slot {
    struct nas_nuttx_virtio_blk block;
    const struct nas_virtio_transport *transport;
};
static struct disk_slot disks[NAS_NUTTX_MAX_BLOCK];
static const char *const disk_names[NAS_NUTTX_MAX_BLOCK] = {
    "virtio0", "virtio1", "virtio2", "virtio3"
};
static unsigned disk_count;
static bool match(const struct nas_bus_device *device) {
    return device->device_id==VIRTIO_ID_BLOCK && device->context!=NULL;
}
static int probe(struct nas_bus_device *device) {
    const struct nas_virtio_transport *tr=device->context;
    unsigned free_slot=NAS_NUTTX_MAX_BLOCK;
    for(unsigned i=0;i<NAS_NUTTX_MAX_BLOCK;i++) {
        if(disks[i].transport==tr)return -EEXIST;
        if(!disks[i].transport && free_slot==NAS_NUTTX_MAX_BLOCK)
            free_slot=i;
    }
    if(free_slot==NAS_NUTTX_MAX_BLOCK)return -ENOSPC;
    struct disk_slot *slot=&disks[free_slot];
    int rc=nas_nuttx_virtio_blk_attach(&slot->block,tr,disk_names[free_slot]);
    if(rc)return rc;
    slot->transport=tr;
    disk_count++;
    return 0;
}
static int remove_device(struct nas_bus_device *device) {
    for(unsigned i=0;i<NAS_NUTTX_MAX_BLOCK;i++) {
        struct disk_slot *slot=&disks[i];
        if(slot->transport!=device->context)continue;
        int rc=nas_nuttx_virtio_blk_detach(&slot->block);
        if(rc)return rc;
        memset(slot,0,sizeof(*slot));
        if(!disk_count)return -EIO;
        disk_count--;
        return 0;
    }
    return -ENODEV;
}
static const struct nas_bus_driver driver={
    .name="nuttx-virtio-blk-port",.bus_mask=NAS_BUS_MASK_ALL,
    .match=match,.probe=probe,.remove=remove_device
};
unsigned nas_nuttx_virtio_blk_count(void){return disk_count;}
int nas_nuttx_virtio_blk_driver_register(void) {
    return nas_bus_driver_register(&driver);
}
