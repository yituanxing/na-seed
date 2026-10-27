#ifndef NASEED_PORT_BLOCK_H
#define NASEED_PORT_BLOCK_H
#include "../../block/device.h"
#include <stdint.h>

/* Port boundary for existing C block drivers (NuttX/NaOS-style or Zephyr-style).
 * This is an ABI adapter, NOT a second block-device registry or VFS layer.
 * Every registered device is still a normal NaSeed BlockDevice.
 *
 * SECTORS: successful callbacks return exactly the completed sector count;
 * ZERO:    successful callbacks return zero.
 * Failures in both forms must return negative errno. In particular, a short
 * transfer is never reported as successful.
 */
enum nas_port_block_result {
    NAS_PORT_BLOCK_ZERO = 0,
    NAS_PORT_BLOCK_SECTORS = 1
};
struct nas_port_block_ops {
    int64_t (*read)(void *backend, uint64_t lba, uint32_t count, void *dst);
    int64_t (*write)(void *backend, uint64_t lba, uint32_t count, const void *src);
    int (*flush)(void *backend);
};
struct nas_port_block {
    struct nas_block_device block;
    void *backend;                         /* Driver owns backing object/lifetime. */
    const struct nas_port_block_ops *port_ops;
    enum nas_port_block_result result;
};
/* Caller must provide zero-initialized lifetime-stable port storage.
 * Attaching a writable filesystem requires a real flush callback.
 * Detach fails while a VFS mount or File pins the BlockDevice.
 */
int nas_port_block_attach(struct nas_port_block *port, const char *name,
                          void *backend, uint64_t sectors,
                          uint32_t sector_bytes, uint32_t max_sectors_per_io,
                          bool read_only, enum nas_port_block_result result,
                          const struct nas_port_block_ops *ops);
int nas_port_block_detach(struct nas_port_block *port);
#endif
