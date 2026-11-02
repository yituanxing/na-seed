#ifndef NASEED_CHRDEV_H
#define NASEED_CHRDEV_H
#include <stdint.h>

/* Device endpoint publication, independent of the bus that discovered it.
 * The VFS is always consulted first: this registry describes only genuinely
 * unmounted bootstrap character nodes, until a mounted devfs owns them. */
struct nas_chrdev_info {
    uint8_t fd_kind;
    unsigned unit;
    uint64_t dev, ino, rdev, mode, size;
};
struct nas_chrdev_provider {
    const char *name;
    /* -ENOENT means that this provider does not own the spelling. */
    int (*describe)(const char *absolute, struct nas_chrdev_info *out);
    /* Called once per new open-file description, never per dup/fork FD. */
    int (*open)(unsigned description_slot, unsigned unit);
};
int nas_chrdev_provider_register(const struct nas_chrdev_provider *provider);
int nas_chrdev_describe(const char *absolute, struct nas_chrdev_info *out,
                       const struct nas_chrdev_provider **provider);
#endif
