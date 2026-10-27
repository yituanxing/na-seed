#ifndef NASEED_UNIKRAFT_MINIMAL_CONFIG_SHIM_H
#define NASEED_UNIKRAFT_MINIMAL_CONFIG_SHIM_H
/* The pinned upstream virtio_blk.h includes Unikraft's platform-heavy
 * virtio_config.h although it only uses its shared integer types here.
 * This compatibility shim belongs to ports/, NOT third_party/.
 *
 * Runtime feature negotiation, PCI/MMIO registers, status and queues
 * are provided by NaSeed's kernel/drivers/virtio/transport.h instead.
 * Never use this small header to compile the full upstream driver:
 * that driver additionally requires ukblkdev, sglist, virtqueue and bus.
 */
#include <virtio/virtio_types.h>
#endif
