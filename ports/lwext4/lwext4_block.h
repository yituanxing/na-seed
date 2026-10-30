#ifndef NASEED_LWEXT4_BLOCK_H
#define NASEED_LWEXT4_BLOCK_H
/* Exact upstream ABI, not a second hand-written ext4 block definition.
 * Build with pinned lwext4 include paths from the dependency manifest. */
#include <ext4_blockdev.h>
#include "../../kernel/block/device.h"
#include <stdbool.h>
#include <stdint.h>
struct nas_lwext4_device {
    struct ext4_blockdev ext;
    struct ext4_blockdev_iface interface;
    struct nas_block_device *storage;
    bool registered;
    uint8_t physical_buffer[4096];
};
/* Pin one registered BlockDevice while lwext4 has its own open/cache state.
 * Caller must unmount and close all upstream files before detach. */
int nas_lwext4_device_attach(struct nas_lwext4_device *dev,
                             const char *block_name, const char *ext_name);
int nas_lwext4_device_detach(struct nas_lwext4_device *dev,
                             const char *ext_name);
#endif
