#ifndef NAS_UBOOT_VIRTIO_H
#define NAS_UBOOT_VIRTIO_H
#include "dm.h"
#include "virtio_types.h"
struct virtqueue;
struct virtio_dev_priv {
 uint32_t vendor;
 uint64_t features;
 uint64_t offered;
};
static inline u32 cpu_to_virtio32(struct udevice *d,u32 v){(void)d;return v;}
static inline u64 cpu_to_virtio64(struct udevice *d,u64 v){(void)d;return v;}
bool virtio_has_feature(struct udevice *,unsigned);
void virtio_driver_features_init(struct virtio_dev_priv *,
 const u32 *,size_t,const u32 *,size_t);
int virtio_find_vqs(struct udevice *,int,struct virtqueue **);
int virtio_reset(struct udevice *);
void nas_uboot_virtio_cread(struct udevice *,size_t,size_t,void *);
#define virtio_cread(dev,type,field,dst) \
 nas_uboot_virtio_cread((dev),offsetof(type,field),sizeof(*(dst)),(dst))
#endif
