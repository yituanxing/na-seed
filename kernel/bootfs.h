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
    uint64_t mtime_sec; /* literal newc c_mtime; immutable on CPIO */
    uint32_t mode;
    struct bootfs_entry *hash_next; /* Private index link, stable slab address. */
};
bool bootfs_mount(const void *bytes, uint64_t size);
bool bootfs_mounted(void);
const struct bootfs_entry *bootfs_find(const char *absolute);
const struct bootfs_entry *bootfs_child(const struct bootfs_entry *dir, unsigned ordinal);
bool bootfs_is_dir(const struct bootfs_entry *entry);
/* Return 0 and the canonical path of an archive entry, or negative Linux
 * errno. Follow intermediate links and, optionally, the final link.
 * This is deliberately a bounded read-only archive pathname resolver.
 */
int bootfs_resolve(const char *absolute, bool follow_final, char out[256]);
bool bootfs_is_link(const struct bootfs_entry *entry);
#endif
