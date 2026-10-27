#ifndef NASEED_BLOCK_DEVICE_H
#define NASEED_BLOCK_DEVICE_H
#include <stdbool.h>
#include <stdint.h>

/* Kernel-buffer-only block device contract. All ISAs share this ABI.
 * Drivers own storage, transport and request completion; filesystems own
 * inodes; VFS owns mounts. No user pointers or syscall numbers enter here.
 *
 * A registered device is allocated and kept alive by its driver. Mounts
 * acquire a reference before using it. Unregister refuses a live mount.
 * All operations are synchronous from a kernel C continuation, for now.
 */
#define NAS_BLOCK_DEVICE_LIMIT 16U

struct nas_block_device;
struct nas_block_ops {
    int (*read)(struct nas_block_device *, uint64_t sector,
                uint32_t sectors, void *kernel_dst);
    int (*write)(struct nas_block_device *, uint64_t sector,
                 uint32_t sectors, const void *kernel_src);
    /* Return 0 only when pending writes satisfy the device's flush policy.
     * NULL means flush is unsupported, not an imaginary successful sync. */
    int (*flush)(struct nas_block_device *);
};
struct nas_block_device {
    const char *name;                 /* Driver-owned, stable through unregistration */
    uint64_t sectors;                 /* Count of logical sectors */
    uint32_t sector_bytes;            /* Power of two, 512 .. 4096 */
    uint32_t max_sectors_per_io;      /* One driver callback maximum */
    bool read_only;
    const struct nas_block_ops *ops;
    void *context;                    /* Driver state, never a user address */
    unsigned refs;                    /* Mount/device-open pins, not raw I/O count */
    bool registered;
};

int nas_block_register(struct nas_block_device *dev);
int nas_block_unregister(struct nas_block_device *dev);
/* A returned reference must be released. A failed unregister leaves state
 * untouched so mounted filesystems can keep using the device. */
struct nas_block_device *nas_block_get(const char *name);
void nas_block_put(struct nas_block_device *dev);
int nas_block_read(struct nas_block_device *dev, uint64_t sector,
                   uint32_t sectors, void *kernel_dst);
int nas_block_write(struct nas_block_device *dev, uint64_t sector,
                    uint32_t sectors, const void *kernel_src);
int nas_block_flush(struct nas_block_device *dev);
#endif
