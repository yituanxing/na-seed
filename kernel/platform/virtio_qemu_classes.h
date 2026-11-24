#ifndef NASEED_VIRTIO_QEMU_CLASSES_H
#define NASEED_VIRTIO_QEMU_CLASSES_H
#include "../drivers/core/bus.h"
#include "../drivers/serial.h"
#include "../../ports/uboot/virtio_bus.h"
#include "../../ports/uboot/virtio_net_bus.h"
#ifdef NAS_ENABLE_VIRTIO_INPUT
#include "../drivers/input/virtio_input.h"
#endif
#ifdef NAS_ENABLE_VIRTIO_GPU
#include "../drivers/display/virtio_gpu.h"
#endif
#ifdef NAS_ENABLE_VIRTIO_BLK_ASYNC
#include "../../ports/uboot/async_blk.h"
#endif
#include <virtio/virtio_ids.h>

/* Transport owners enumerate and route resources; the existing bus owns
 * class binding. Keep identical registration order on PCI and MMIO. */
static inline int nas_qemu_virtio_register_classes(void) {
    int rc=0;
#ifdef NAS_ENABLE_VIRTIO_BLK_ASYNC
    rc=nas_async_blk_driver_register();
    if(rc<0)return rc;
#endif
#ifdef NAS_ENABLE_VIRTIO_BLK
    rc=nas_virtio_blk_driver_register();
    if(rc<0)return rc;
#endif
#ifdef NAS_ENABLE_VIRTIO_NET
    rc=nas_virtio_net_driver_register();
    if(rc<0)return rc;
#endif
#ifdef NAS_ENABLE_VIRTIO_INPUT
    rc=nas_virtio_input_driver_register();
    if(rc<0)return rc;
#endif
#ifdef NAS_ENABLE_VIRTIO_GPU
    rc=nas_virtio_gpu_driver_register();
    if(rc<0)return rc;
#endif
    return 0;
}
static inline bool nas_qemu_virtio_supported_class(uint32_t id) {
    return id==VIRTIO_ID_BLOCK || id==VIRTIO_ID_NET ||
           id==VIRTIO_ID_INPUT || id==VIRTIO_ID_GPU;
}
static inline unsigned nas_qemu_virtio_block_count(void) {
#ifdef NAS_ENABLE_VIRTIO_BLK
    return nas_virtio_blk_count();
#else
    return 0;
#endif
}
static inline unsigned nas_qemu_virtio_net_count(void) {
#ifdef NAS_ENABLE_VIRTIO_NET
    return nas_virtio_net_count();
#else
    return 0;
#endif
}
static inline void nas_qemu_virtio_print_count(const char *name,
                                              const char *transport,unsigned count) {
    nas_print(name);nas_print(" real modern ");nas_print(transport);
    nas_print(" devices=");nas_dec(count);nas_print("\n");
}
static inline void nas_qemu_virtio_report_classes(const char *transport) {
    nas_qemu_virtio_print_count("[VIRTIO-BLK]",transport,nas_qemu_virtio_block_count());
    nas_qemu_virtio_print_count("[VIRTIO-NET]",transport,nas_qemu_virtio_net_count());
#ifdef NAS_ENABLE_VIRTIO_INPUT
    nas_qemu_virtio_print_count("[VIRTIO-INPUT]",transport,nas_virtio_input_count());
#endif
#ifdef NAS_ENABLE_VIRTIO_GPU
    nas_qemu_virtio_print_count("[VIRTIO-GPU]",transport,nas_virtio_gpu_count());
#endif
#ifdef NAS_ENABLE_VIRTIO_BLK_ASYNC
    nas_qemu_virtio_print_count("[VIRTIO-BLK-ASYNC]",transport,nas_async_blk_count());
#endif
}
#endif
