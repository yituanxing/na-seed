#include "bus.h"
#include "../../errno.h"
#include <stddef.h>

/* No hidden heap/device-specific code. Stable storage belongs to discoverer. */
static struct nas_bus_device *devices[NAS_BUS_DEVICE_LIMIT];
static const struct nas_bus_driver *drivers[NAS_BUS_DRIVER_LIMIT];
static unsigned pending_removals;
static unsigned next_removal;
static bool valid_bus(enum nas_bus_type type) {
    return type == NAS_BUS_VIRTIO_MMIO || type == NAS_BUS_PCI;
}
static bool valid_resource(const struct nas_bus_device *dev) {
    const struct nas_bus_resource *res = &dev->resource;
    if (dev->bus == NAS_BUS_VIRTIO_MMIO) {
        return res->kind == NAS_BUS_RESOURCE_VIRTIO_MMIO &&
            res->view.mmio.registers &&
            res->view.mmio.mapped_bytes >= 0x200U &&
            res->view.mmio.mapped_bytes <= 4096U &&
            !(res->view.mmio.mapped_bytes & 3U);
    }
    if (dev->bus == NAS_BUS_PCI) {
        return res->kind == NAS_BUS_RESOURCE_PCI &&
            res->view.pci.config &&
            res->view.pci.config_bytes == NAS_BUS_PCI_CONFIG_BYTES &&
            res->view.pci.slot < 32U &&
            res->view.pci.function < 8U &&
            res->view.pci.mapped_bar_mask &&
            !(res->view.pci.mapped_bar_mask & ~0x3fU);
    }
    return false;
}
static int bind(struct nas_bus_device *dev) {
    if (dev->driver || dev->removal_pending) return 0;
    for (unsigned i = 0; i < NAS_BUS_DRIVER_LIMIT; ++i) {
        const struct nas_bus_driver *drv = drivers[i];
        if (!drv || !(drv->bus_mask & (1U << dev->bus)) ||
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
        if (!devices[i] || devices[i]->driver ||
            devices[i]->removal_pending) continue;
        int rc = bind(devices[i]);
        if (rc && !first) first = rc;
    }
    return first;
}
int nas_bus_driver_register(const struct nas_bus_driver *drv) {
    if (!drv || !(drv->bus_mask & NAS_BUS_MASK_ALL) ||
        (drv->bus_mask & ~NAS_BUS_MASK_ALL) ||
        !drv->name || !drv->name[0] || !drv->probe) return -EINVAL;
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
    if (!dev || !valid_bus(dev->bus) || !valid_resource(dev) ||
        dev->registered || dev->driver || dev->removal_pending) return -EINVAL;
    unsigned vacant = NAS_BUS_DEVICE_LIMIT;
    for (unsigned i = 0; i < NAS_BUS_DEVICE_LIMIT; ++i) {
        if (devices[i] == dev) return -EEXIST;
        if (!devices[i] && vacant == NAS_BUS_DEVICE_LIMIT) vacant = i;
    }
    if (vacant == NAS_BUS_DEVICE_LIMIT) return -ENOSPC;
    devices[vacant] = dev;
    dev->registered = true;
    dev->removal_error = 0;
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
static int unregister_now(struct nas_bus_device *dev) {
    if (!dev || !dev->registered) return -ENODEV;
    if (dev->driver) return -EBUSY; /* Explicit unbind first. */
    for (unsigned i = 0; i < NAS_BUS_DEVICE_LIMIT; ++i) {
        if (devices[i] == dev) {
            devices[i] = NULL;
            dev->registered = false;
            dev->removal_error = 0;
            return 0;
        }
    }
    return -ENODEV;
}
int nas_bus_device_unregister(struct nas_bus_device *dev) {
    if (dev && dev->removal_pending) return -EBUSY;
    return unregister_now(dev);
}
int nas_bus_device_request_remove(struct nas_bus_device *dev) {
    if (!dev || !dev->registered) return -ENODEV;
    if (dev->removal_pending) return 0;
    if (dev->driver && !dev->driver->remove) return -EOPNOTSUPP;
    /* This caller is a normal kernel execution context, not an IRQ.
     * Net drivers quiesce new traffic NOW; block drivers may keep servicing
     * mounted volumes until their last BlockDevice reference is put. */
    int rc = nas_bus_device_unbind(dev);
    if (!rc) rc = unregister_now(dev);
    if (!rc) { dev->removal_error = 0; return 0; }
    if (rc != -EBUSY && rc != -EAGAIN) {
        dev->removal_error = rc;
        return rc;
    }
    dev->removal_pending = true;
    dev->removal_error = 0;
    ++pending_removals;
    return 0;
}
unsigned nas_bus_service_removals(unsigned budget) {
    if (!budget || !pending_removals) return 0;
    unsigned attempts = 0, scanned = 0;
    while (attempts < budget && scanned < NAS_BUS_DEVICE_LIMIT &&
           pending_removals) {
        const unsigned index = next_removal;
        next_removal = (next_removal + 1U) % NAS_BUS_DEVICE_LIMIT;
        ++scanned;
        struct nas_bus_device *dev = devices[index];
        if (!dev || !dev->removal_pending) continue;
        ++attempts;
        int rc = nas_bus_device_unbind(dev);
        if (!rc) rc = unregister_now(dev);
        if (rc == -EBUSY || rc == -EAGAIN) continue;
        dev->removal_pending = false;
        dev->removal_error = rc;
        --pending_removals;
    }
    return attempts;
}
int nas_bus_device_remove_status(const struct nas_bus_device *dev) {
    if (!dev) return -EINVAL;
    if (dev->removal_pending) return -EINPROGRESS;
    if (dev->removal_error) return dev->removal_error;
    return dev->registered ? -EAGAIN : 0;
}
