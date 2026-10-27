#include "bus.h"
#include "../../errno.h"
#include <stddef.h>

/* No hidden heap/device-specific code. Stable storage belongs to discoverer. */
static struct nas_bus_device *devices[NAS_BUS_DEVICE_LIMIT];
static const struct nas_bus_driver *drivers[NAS_BUS_DRIVER_LIMIT];
static bool valid_bus(enum nas_bus_type type) {
    return type == NAS_BUS_VIRTIO_MMIO || type == NAS_BUS_PCI;
}
static int bind(struct nas_bus_device *dev) {
    if (dev->driver) return 0;
    for (unsigned i = 0; i < NAS_BUS_DRIVER_LIMIT; ++i) {
        const struct nas_bus_driver *drv = drivers[i];
        if (!drv || drv->bus != dev->bus ||
            (drv->match && !drv->match(dev))) continue;
        int rc = drv->probe(dev);
        if (rc == 0) { dev->driver = drv; return 0; }
        if (rc != -ENODEV) return rc;
    }
    return 0; /* Device is valid but no driver claimed it (yet). */
}
int nas_bus_rescan(void) {
    int first = 0;
    for (unsigned i = 0; i < NAS_BUS_DEVICE_LIMIT; ++i) {
        if (!devices[i] || devices[i]->driver) continue;
        int rc = bind(devices[i]);
        if (rc && !first) first = rc;
    }
    return first;
}
int nas_bus_driver_register(const struct nas_bus_driver *drv) {
    if (!drv || !valid_bus(drv->bus) || !drv->name || !drv->name[0] ||
        !drv->probe) return -EINVAL;
    unsigned vacant = NAS_BUS_DRIVER_LIMIT;
    for (unsigned i = 0; i < NAS_BUS_DRIVER_LIMIT; ++i) {
        if (drivers[i] == drv) return -EEXIST;
        if (!drivers[i] && vacant == NAS_BUS_DRIVER_LIMIT) vacant = i;
    }
    if (vacant == NAS_BUS_DRIVER_LIMIT) return -ENOSPC;
    drivers[vacant] = drv;
    /* A late-arriving driver may bind existing unclaimed devices. If a
     * probe fails, the driver remains registered for an explicit rescan. */
    return nas_bus_rescan();
}
int nas_bus_driver_unregister(const struct nas_bus_driver *drv) {
    if (!drv) return -EINVAL;
    unsigned found = NAS_BUS_DRIVER_LIMIT;
    for (unsigned i = 0; i < NAS_BUS_DRIVER_LIMIT; ++i)
        if (drivers[i] == drv) { found = i; break; }
    if (found == NAS_BUS_DRIVER_LIMIT) return -ENODEV;
    for (unsigned i = 0; i < NAS_BUS_DEVICE_LIMIT; ++i)
        if (devices[i] && devices[i]->driver == drv) return -EBUSY;
    drivers[found] = NULL;
    return 0;
}
int nas_bus_device_register(struct nas_bus_device *dev) {
    if (!dev || !valid_bus(dev->bus) || !dev->resource ||
        dev->registered || dev->driver) return -EINVAL;
    unsigned vacant = NAS_BUS_DEVICE_LIMIT;
    for (unsigned i = 0; i < NAS_BUS_DEVICE_LIMIT; ++i) {
        if (devices[i] == dev) return -EEXIST;
        if (!devices[i] && vacant == NAS_BUS_DEVICE_LIMIT) vacant = i;
    }
    if (vacant == NAS_BUS_DEVICE_LIMIT) return -ENOSPC;
    devices[vacant] = dev;
    dev->registered = true;
    int rc = bind(dev);
    if (rc) { devices[vacant] = NULL; dev->registered = false; }
    return rc;
}
int nas_bus_device_unbind(struct nas_bus_device *dev) {
    if (!dev || !dev->registered) return -ENODEV;
    if (!dev->driver) return 0;
    if (!dev->driver->remove) return -EOPNOTSUPP;
    int rc = dev->driver->remove(dev);
    if (!rc) dev->driver = NULL;
    return rc;
}
int nas_bus_device_unregister(struct nas_bus_device *dev) {
    if (!dev || !dev->registered) return -ENODEV;
    if (dev->driver) return -EBUSY; /* Explicit unbind first. */
    for (unsigned i = 0; i < NAS_BUS_DEVICE_LIMIT; ++i) {
        if (devices[i] == dev) {
            devices[i] = NULL;
            dev->registered = false;
            return 0;
        }
    }
    return -ENODEV;
}
