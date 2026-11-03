#ifndef NASEED_CHRDEV_H
#define NASEED_CHRDEV_H
#include <stdbool.h>
#include <stdint.h>

/* Character-device publication, independent of discovery bus and Linux
 * syscall dispatch. The VFS owns all mounted inodes; these node identities
 * describe only paths absent from the bootstrap namespace until devfs mounts.
 *
 * Owners provide stable storage, register when their class device is live,
 * and may unregister only after the last shared OpenFile has closed. This
 * is the devfs publication boundary, not a second pathname walker.
 */
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
struct nas_chrdev_node {
    const char *path;                 /* Owner-owned immutable /dev pathname */
    struct nas_chrdev_info info;      /* Device and stable stat identity */
    const struct nas_chrdev_provider *provider; /* Optional per-open backend */
    uint64_t devfs_ino;              /* Unique per registration incarnation */
    bool (*present)(unsigned unit);   /* Optional non-consuming availability */
    unsigned pins;                   /* One per owning shared OpenFile */
    bool registered;
};
int nas_chrdev_node_register(struct nas_chrdev_node *node);
/* -EBUSY on open descriptions; no user-visible alias may dangle. */
int nas_chrdev_node_unregister(struct nas_chrdev_node *node);
int nas_chrdev_node_pin(struct nas_chrdev_node *node);
void nas_chrdev_node_put(struct nas_chrdev_node *node);
/* A borrowed live node, or NULL after the last registered present node.
 * Enumeration never creates a second class/device registry in devfs. */
struct nas_chrdev_node *nas_chrdev_node_at(unsigned ordinal);
int nas_chrdev_resolve_rdev(uint64_t rdev,struct nas_chrdev_info *out,
                            const struct nas_chrdev_provider **provider,
                            struct nas_chrdev_node **node);
int nas_chrdev_provider_register(const struct nas_chrdev_provider *provider);
int nas_chrdev_describe(const char *absolute, struct nas_chrdev_info *out,
                       const struct nas_chrdev_provider **provider,
                       struct nas_chrdev_node **node);
#endif
