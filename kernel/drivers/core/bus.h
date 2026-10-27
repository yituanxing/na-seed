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
};
struct nas_bus_driver {
    const char *name;
    enum nas_bus_type bus;
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
#endif
