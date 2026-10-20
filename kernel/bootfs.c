#include "bootfs.h"
#include <stddef.h>

#define MAX_ENTRIES 96
#define NEWC_HEADER 110ULL
#define S_IFMT 0170000U
#define S_IFREG 0100000U
#define S_IFDIR 0040000U

static struct bootfs_entry entries[MAX_ENTRIES];
static unsigned entry_count;
static bool mounted;

static bool equal(const char *a, const char *b) {
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static bool prefix(const char *a, const char *b) {
    while (*b) if (*a++ != *b++) return false;
    return true;
}
static bool hex8(const uint8_t *p, uint32_t *out) {
    uint32_t result = 0;
    for (unsigned i = 0; i < 8; ++i) {
        uint8_t c = p[i];
        uint32_t x;
        if (c >= '0' && c <= '9') x = c - '0';
        else if (c >= 'a' && c <= 'f') x = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') x = c - 'A' + 10;
        else return false;
        result = (result << 4) | x;
    }
    *out = result;
    return true;
}
static bool aligned4(uint64_t n, uint64_t limit, uint64_t *out) {
    if (n > limit || n > UINT64_MAX - 3) return false;
    *out = (n + 3) & ~3ULL;
    return *out <= limit;
}
/* Explicitly refuse traversal, duplicate names, and unsupported entry types.
 * The archive itself is made from a fixed, reviewed fixture at build time.
 */
static bool archive_name(struct bootfs_entry *entry,
                         const uint8_t *raw, uint32_t len) {
    if (len < 2 || raw[len - 1] != 0) return false;
    for (unsigned i = 0; i + 1 < len; i++)
        if (raw[i] == 0) return false;
    if (len == 2 && raw[0] == '.') {
        entry->path[0] = '/';
        entry->path[1] = 0;
        return true;
    }
    unsigned start = (raw[0] == '.' && raw[1] == '/') ? 2 : 0;
    unsigned end = len - 1;
    if (start == end) return false;
    while (end > start && raw[end - 1] == '/') end--;
    if (start == end || end - start + 2 > sizeof(entry->path)) return false;
    entry->path[0] = '/';
    unsigned used = 1;
    unsigned seg_start = start;
    for (unsigned i = start; i <= end; i++) {
        if (i < end && raw[i] != '/') continue;
        unsigned n = i - seg_start;
        if (!n || (n == 1 && raw[seg_start] == '.') ||
            (n == 2 && raw[seg_start] == '.' && raw[seg_start + 1] == '.'))
            return false;
        seg_start = i + 1;
    }
    for (unsigned i = start; i < end; i++) entry->path[used++] = (char)raw[i];
    entry->path[used] = 0;
    return true;
}
bool bootfs_mount(const void *bytes, uint64_t size) {
    mounted = false;
    entry_count = 0;
    if (!bytes || size < NEWC_HEADER) return false;
    const uint8_t *base = (const uint8_t *)bytes;
    uint64_t at = 0;
    while (at < size) {
        if (size - at < NEWC_HEADER) return false;
        const uint8_t *h = base + at;
        if (!prefix((const char *)h, "070701")) return false;
        uint32_t file_size, name_size, mode;
        if (!hex8(h + 54, &file_size) ||
            !hex8(h + 94, &name_size) ||
            !hex8(h + 14, &mode) ||
            !name_size || name_size > 256) return false;
        uint64_t name_at = at + NEWC_HEADER;
        if (name_size > size - name_at) return false;
        const uint8_t *name = base + name_at;
        uint64_t data_at;
        if (!aligned4(name_at + name_size, size, &data_at) ||
            file_size > size - data_at) return false;
        uint64_t next;
        if (!aligned4(data_at + file_size, size, &next)) return false;
        if (name_size == 11 && equal((const char *)name, "TRAILER!!!")) {
            if (file_size) return false;
            mounted = entry_count > 0 && bootfs_find("/") != 0;
            return mounted;
        }
        if (entry_count >= MAX_ENTRIES) return false;
        unsigned type = mode & S_IFMT;
        if (type != S_IFREG && type != S_IFDIR) return false;
        if (type == S_IFDIR && file_size) return false;
        struct bootfs_entry *entry = &entries[entry_count];
        if (!archive_name(entry, name, name_size)) return false;
        for (unsigned n = 0; n < entry_count; n++)
            if (equal(entries[n].path, entry->path)) return false;
        entry->mode = mode;
        entry->size = file_size;
        entry->data = base + data_at;
        entry->ino = entry_count + 1;
        entry_count++;
        at = next;
    }
    return false;
}
bool bootfs_mounted(void) { return mounted; }
const struct bootfs_entry *bootfs_find(const char *absolute) {
    if (!absolute) return 0;
    for (unsigned i = 0; i < entry_count; ++i)
        if (equal(entries[i].path, absolute)) return &entries[i];
    return 0;
}
bool bootfs_is_dir(const struct bootfs_entry *entry) {
    return entry && (entry->mode & S_IFMT) == S_IFDIR;
}
const struct bootfs_entry *bootfs_child(const struct bootfs_entry *dir,
                                        unsigned ordinal) {
    if (!bootfs_is_dir(dir)) return 0;
    const char *parent = dir->path;
    unsigned plen = 0;
    while (parent[plen]) plen++;
    for (unsigned i = 0; i < entry_count; i++) {
        const char *candidate = entries[i].path;
        if (!prefix(candidate, parent)) continue;
        const char *tail = candidate + plen;
        if (plen != 1) {
            if (*tail++ != '/') continue;
        }
        if (!*tail) continue;
        bool direct = true;
        for (const char *p = tail; *p; ++p)
            if (*p == '/') { direct = false; break; }
        if (!direct) continue;
        if (ordinal-- == 0) return &entries[i];
    }
    return 0;
}
