#ifndef NASEED_BOOTFS_H
#define NASEED_BOOTFS_H
#include <stdint.h>
#include <stdbool.h>

/* Read-only, zero-copy index of a Limine-provided CPIO newc archive.
 * An archive is data, not an executable loader or a general VFS.
 */
struct bootfs_entry {
    char path[256];
    const uint8_t *data;
    uint64_t size;
    uint64_t ino;
    uint32_t mode;
};
bool bootfs_mount(const void *bytes, uint64_t size);
bool bootfs_mounted(void);
const struct bootfs_entry *bootfs_find(const char *absolute);
const struct bootfs_entry *bootfs_child(const struct bootfs_entry *dir, unsigned ordinal);
bool bootfs_is_dir(const struct bootfs_entry *entry);
#endif
