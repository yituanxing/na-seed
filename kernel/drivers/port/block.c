#include "block.h"
#include "../../errno.h"
#include <stddef.h>
#include <stdint.h>

/* The only conversion from third-party return conventions to NaSeed's
 * 0/-errno block ABI. No driver-specific logic enters VFS or syscalls.
 */
static int convert_result(int64_t observed, uint32_t expected,
                          enum nas_port_block_result mode) {
    if (observed < 0) {
        /* A legacy upstream callback using -1 as generic failure. */
        if (observed == -1 || observed < INT32_MIN) return -EIO;
        return (int)observed;
    }
    if (mode == NAS_PORT_BLOCK_ZERO)
        return observed == 0 ? 0 : -EIO;
    return observed == (int64_t)expected ? 0 : -EIO;
}
static int block_read(struct nas_block_device *dev, uint64_t lba,
                      uint32_t count, void *dst) {
    struct nas_port_block *port = (struct nas_port_block *)dev->context;
    return convert_result(port->port_ops->read(port->backend, lba, count, dst),
                          count, port->result);
}
static int block_write(struct nas_block_device *dev, uint64_t lba,
                       uint32_t count, const void *src) {
    struct nas_port_block *port = (struct nas_port_block *)dev->context;
    return convert_result(port->port_ops->write(port->backend, lba, count, src),
                          count, port->result);
}
static int block_flush(struct nas_block_device *dev) {
    struct nas_port_block *port = (struct nas_port_block *)dev->context;
    return port->port_ops->flush ?
        port->port_ops->flush(port->backend) : -EOPNOTSUPP;
}
static const struct nas_block_ops block_ops = {
    .read = block_read, .write = block_write, .flush = block_flush
};
int nas_port_block_attach(struct nas_port_block *port, const char *name,
                          void *backend, uint64_t sectors,
                          uint32_t sector_bytes, uint32_t max_sectors_per_io,
                          bool read_only, enum nas_port_block_result result,
                          const struct nas_port_block_ops *ops) {
    if (!port || !backend || !ops || !ops->read ||
        (!read_only && (!ops->write || !ops->flush)) ||
        (result != NAS_PORT_BLOCK_ZERO && result != NAS_PORT_BLOCK_SECTORS))
        return -EINVAL;
    if (port->block.registered || port->block.refs) return -EBUSY;
    port->backend = backend;
    port->port_ops = ops;
    port->result = result;
    port->block = (struct nas_block_device){
        .name = name, .sectors = sectors, .sector_bytes = sector_bytes,
        .max_sectors_per_io = max_sectors_per_io, .read_only = read_only,
        .ops = &block_ops, .context = port
    };
    return nas_block_register(&port->block);
}
int nas_port_block_detach(struct nas_port_block *port) {
    if (!port) return -EINVAL;
    return nas_block_unregister(&port->block);
}
