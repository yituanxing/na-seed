#include "device.h"
#include "../errno.h"
#include "../lib/string.h"
#include <stddef.h>
#include <stdint.h>

static struct nas_block_device *registered[NAS_BLOCK_DEVICE_LIMIT];

static bool sector_size_valid(uint32_t n) {
    return n >= 512U && n <= 4096U && !(n & (n - 1U));
}
int nas_block_register(struct nas_block_device *dev) {
    if (!dev || !dev->name || !dev->name[0] || !dev->ops ||
        !dev->ops->read || !sector_size_valid(dev->sector_bytes) ||
        !dev->sectors || !dev->max_sectors_per_io ||
        dev->sectors > UINT64_MAX / dev->sector_bytes ||
        (!dev->read_only && !dev->ops->write))
        return -EINVAL;
    if (dev->registered || dev->refs)
        return -EBUSY;
    unsigned vacant = NAS_BLOCK_DEVICE_LIMIT;
    for (unsigned n = 0; n < NAS_BLOCK_DEVICE_LIMIT; ++n) {
        if (registered[n] && !strcmp(registered[n]->name, dev->name))
            return -EEXIST;
        if (!registered[n] && vacant == NAS_BLOCK_DEVICE_LIMIT)
            vacant = n;
    }
    if (vacant == NAS_BLOCK_DEVICE_LIMIT)
        return -ENOSPC;
    registered[vacant] = dev;
    dev->registered = true;
    return 0;
}
int nas_block_unregister(struct nas_block_device *dev) {
    if (!dev || !dev->registered) return -ENODEV;
    if (dev->refs) return -EBUSY;
    for (unsigned n = 0; n < NAS_BLOCK_DEVICE_LIMIT; ++n) {
        if (registered[n] == dev) {
            registered[n] = NULL;
            dev->registered = false;
            return 0;
        }
    }
    return -ENODEV;
}
struct nas_block_device *nas_block_get(const char *name) {
    if (!name) return NULL;
    for (unsigned n = 0; n < NAS_BLOCK_DEVICE_LIMIT; ++n) {
        struct nas_block_device *dev = registered[n];
        if (dev && dev->registered && !strcmp(name, dev->name)) {
            ++dev->refs;
            return dev;
        }
    }
    return NULL;
}
void nas_block_put(struct nas_block_device *dev) {
    if (dev && dev->refs) --dev->refs;
}
static int check_io(struct nas_block_device *dev, uint64_t sector,
                    uint32_t count, const void *buf, bool write) {
    if (!dev || !dev->registered) return -ENODEV;
    if (!count) return 0;
    if (!buf || sector > dev->sectors || count > dev->sectors - sector)
        return -EINVAL;
    if ((uint64_t)count > SIZE_MAX / dev->sector_bytes) return -EOVERFLOW;
    if (write && dev->read_only) return -EROFS;
    if (!dev->ops || (write ? !dev->ops->write : !dev->ops->read))
        return -EOPNOTSUPP;
    return 0;
}
int nas_block_read(struct nas_block_device *dev, uint64_t sector,
                   uint32_t count, void *buf) {
    int err = check_io(dev, sector, count, buf, false);
    if (err || !count) return err;
    uint32_t left = count;
    uint8_t *next = (uint8_t *)buf;
    while (left) {
        uint32_t step = left > dev->max_sectors_per_io ?
                        dev->max_sectors_per_io : left;
        err = dev->ops->read(dev, sector, step, next);
        if (err) return err;
        sector += step;
        next += (size_t)step * dev->sector_bytes;
        left -= step;
    }
    return 0;
}
int nas_block_write(struct nas_block_device *dev, uint64_t sector,
                    uint32_t count, const void *buf) {
    int err = check_io(dev, sector, count, buf, true);
    if (err || !count) return err;
    uint32_t left = count;
    const uint8_t *next = (const uint8_t *)buf;
    while (left) {
        uint32_t step = left > dev->max_sectors_per_io ?
                        dev->max_sectors_per_io : left;
        err = dev->ops->write(dev, sector, step, next);
        if (err) return err;
        sector += step;
        next += (size_t)step * dev->sector_bytes;
        left -= step;
    }
    return 0;
}
int nas_block_flush(struct nas_block_device *dev) {
    if (!dev || !dev->registered) return -ENODEV;
    if (dev->read_only) return 0; /* No writable data to synchronize. */
    if (!dev->ops || !dev->ops->flush) return -EOPNOTSUPP;
    return dev->ops->flush(dev);
}
