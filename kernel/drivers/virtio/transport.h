#ifndef NASEED_VIRTIO_TRANSPORT_H
#define NASEED_VIRTIO_TRANSPORT_H
#include <stdint.h>
#include <stdbool.h>
#include "../port/dma.h"
/* VirtIO device/queue operations: transport-independent request backends
 * use only this interface. MMIO, PCI and any later transport implement
 * it once; no block, net or other device protocol logic belongs here.
 */
#define NAS_VIRTIO_STATUS_ACK 1U
#define NAS_VIRTIO_STATUS_DRIVER 2U
#define NAS_VIRTIO_STATUS_DRIVER_OK 4U
#define NAS_VIRTIO_STATUS_FEATURES_OK 8U
#define NAS_VIRTIO_STATUS_FAILED 128U
#define NAS_VIRTIO_F_VERSION_1 (UINT64_C(1)<<32)
struct nas_virtio_transport_ops {
    void (*reset)(void *);
    uint32_t (*read_status)(void *);
    void (*write_status)(void *, uint32_t);
    uint32_t (*read_features)(void *, unsigned bank);
    void (*write_features)(void *, unsigned bank, uint32_t features);
    int (*setup_queue)(void *, unsigned qid, unsigned count,
                       uint64_t desc, uint64_t driver, uint64_t device);
    void (*notify_queue)(void *, unsigned qid);
    uint32_t (*read_config32)(void *, uint32_t offset);
};
struct nas_virtio_transport {
    void *context;
    const struct nas_virtio_transport_ops *ops;
    const struct nas_dma_provider *dma;
};
/* required features absent or missing modern transport => -EOPNOTSUPP.
 * optional features are only accepted when advertised by the hardware.
 * The common lifecycle owns status transitions, not block drivers.
 */
int nas_virtio_negotiate(const struct nas_virtio_transport *, uint64_t required,
                        uint64_t optional, uint64_t *accepted);
int nas_virtio_setup_queue(const struct nas_virtio_transport *, unsigned qid,
                           unsigned count, uint64_t desc,
                           uint64_t driver, uint64_t device);
int nas_virtio_activate(const struct nas_virtio_transport *);
void nas_virtio_notify(const struct nas_virtio_transport *, unsigned qid);
void nas_virtio_fail(const struct nas_virtio_transport *);
uint32_t nas_virtio_config32(const struct nas_virtio_transport *,uint32_t offset);
#endif
