#ifndef NASEED_BUS_DEVICE_H
#define NASEED_BUS_DEVICE_H
#include <stdbool.h>
#include <stdint.h>

/* One kernel bus/driver binding model. A filesystem cannot see any of
 * these objects; storage drivers publish only nas_block_device.
 * Transport discovery owns lifetime-stable bus_device storage, the driver
 * owns its private backend and register/unregister policy.
 */
enum nas_bus_type {
    NAS_BUS_VIRTIO_MMIO = 1,
    NAS_BUS_PCI = 2
};
/* Drivers use a bus-family bitset: a VirtIO block implementation handles
 * the SAME logical class over both MMIO and PCI transport. Do not fork it
 * into one driver per architecture or transport.
 */
#define NAS_BUS_MASK_VIRTIO_MMIO (1U << NAS_BUS_VIRTIO_MMIO)
#define NAS_BUS_MASK_PCI        (1U << NAS_BUS_PCI)
#define NAS_BUS_MASK_ALL        (NAS_BUS_MASK_VIRTIO_MMIO | NAS_BUS_MASK_PCI)
#define NAS_BUS_DEVICE_LIMIT 64U
#define NAS_BUS_DRIVER_LIMIT 16U
struct nas_bus_driver;
struct nas_bus_device {
    enum nas_bus_type bus;
    uint32_t device_id;
    uint32_t vendor_id;
    volatile void *resource; /* MMIO region, or PCI transport descriptor */
    void *context;           /* Discoverer-owned transport state */
    const struct nas_bus_driver *driver; /* NULL until successful probe */
    bool registered;
    /* Only the bus owns an asynchronous removal request. The discoverer
     * keeps this object, its transport and BAR mapping stable until DONE. */
    bool removal_pending;
    int removal_error;       /* terminal driver error, retained for caller */
};
struct nas_bus_driver {
    const char *name;
    uint32_t bus_mask; /* OR of NAS_BUS_MASK_* (not an ISA selector) */
    bool (*match)(const struct nas_bus_device *);
    /* On success, driver may retain device pointer; on failure it must
     * release any partial allocations/registered block devices. */
    int (*probe)(struct nas_bus_device *);
    /* Optional. Reject unbind with -EBUSY while mounts own BlockDevice. */
    int (*remove)(struct nas_bus_device *);
};
int nas_bus_driver_register(const struct nas_bus_driver *);
int nas_bus_driver_unregister(const struct nas_bus_driver *);
int nas_bus_device_register(struct nas_bus_device *);
int nas_bus_device_unbind(struct nas_bus_device *);
int nas_bus_device_unregister(struct nas_bus_device *);
int nas_bus_rescan(void);
/* Request full removal, including unbind and registry deletion. Starts the
 * driver's stop/drain phase immediately; -EBUSY is accepted as a pending
 * request, not returned to the caller for manual retry. Idempotent while
 * pending. No device storage or DMA is freed until driver remove succeeds.
 * Unlike an ordinary unbind, a removal request forbids future rebinding. */
int nas_bus_device_request_remove(struct nas_bus_device *);
/* Runs only at a safe scheduler boundary, never inside IRQ/tcpip_thread.
 * Tries at most `budget` pending removals, round-robin across devices;
 * returns the number attempted. No new permanent kernel worker is needed. */
unsigned nas_bus_service_removals(unsigned budget);
/* 0: completely removed; -EINPROGRESS: waiting for consumers;
 * -EAGAIN: registered, no removal requested; otherwise terminal -errno. */
int nas_bus_device_remove_status(const struct nas_bus_device *);
#endif
