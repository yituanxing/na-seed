#ifndef NASEED_DEVFS_H
#define NASEED_DEVFS_H
#include "vfs.h"

/* Read-only device namespace, not a device registry. The individual class
 * owners publish and withdraw endpoints through nas_chrdev_node_register(). */
extern const struct nas_vfs_type nas_devfs_type;

#endif
