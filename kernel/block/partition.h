#ifndef NASEED_BLOCK_PARTITION_H
#define NASEED_BLOCK_PARTITION_H
#include "device.h"
/* A partition is a real BlockDevice with an owned parent device reference.
 * No filesystem, PCI, DMA or architecture policy appears in this object.
 * The view owns no disk data; caller owns the stable descriptor until detach. */
struct nas_partition_extent {
    uint64_t first_lba;
    uint64_t sectors;
};
struct nas_partition_view {
    struct nas_block_device block;
    struct nas_block_device *parent;
    struct nas_partition_extent extent;
};
int nas_partition_probe(struct nas_block_device *disk, unsigned index,
                        struct nas_partition_extent *extent);
int nas_partition_attach(struct nas_partition_view *view,const char *name,
                         const char *disk_name,
                         const struct nas_partition_extent *extent,
                         bool read_only);
int nas_partition_detach(struct nas_partition_view *view);
#endif
