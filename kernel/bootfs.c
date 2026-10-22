#include "bootfs.h"
#include "errno.h"
#include "lib/string.h"
#include <stddef.h>

#define MAX_ENTRIES 96
#define NEWC_HEADER 110ULL
#define S_IFMT 0170000U
#define S_IFREG 0100000U
#define S_IFDIR 0040000U
#define S_IFLNK 0120000U

static struct bootfs_entry entries[MAX_ENTRIES];
static unsigned entry_count;
static bool mounted;

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
        if (memcmp(h, "070701", 6) != 0) return false;
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
        if (name_size == 11 && memcmp(name, "TRAILER!!!", 11) == 0) {
            if (file_size) return false;
            mounted = entry_count > 0 && bootfs_find("/") != 0;
            return mounted;
        }
        if (entry_count >= MAX_ENTRIES) return false;
        unsigned type = mode & S_IFMT;
        if (type != S_IFREG && type != S_IFDIR && type != S_IFLNK) return false;
        if (type == S_IFDIR && file_size) return false;
        /* In newc, a symlink's bytes are its literal target without NUL. */
        if (type == S_IFLNK) {
            if (!file_size || file_size >= 256) return false;
            for (uint32_t n = 0; n < file_size; n++)
                if (base[data_at + n] == 0) return false;
        }
        struct bootfs_entry *entry = &entries[entry_count];
        if (!archive_name(entry, name, name_size)) return false;
        for (unsigned n = 0; n < entry_count; n++)
            if (strcmp(entries[n].path, entry->path) == 0) return false;
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
bool bootfs_is_link(const struct bootfs_entry *entry) {
    return entry && (entry->mode & S_IFMT) == S_IFLNK;
}
const struct bootfs_entry *bootfs_find(const char *absolute) {
    if (!absolute) return 0;
    for (unsigned i = 0; i < entry_count; ++i)
        if (strcmp(entries[i].path, absolute) == 0) return &entries[i];
    return 0;
}
bool bootfs_is_dir(const struct bootfs_entry *entry) {
    return entry && (entry->mode & S_IFMT) == S_IFDIR;
}
const struct bootfs_entry *bootfs_child(const struct bootfs_entry *dir,
                                        unsigned ordinal) {
    if (!bootfs_is_dir(dir)) return 0;
    const char *parent = dir->path;
    size_t plen = strlen(parent);
    for (unsigned i = 0; i < entry_count; i++) {
        const char *candidate = entries[i].path;
        if (strncmp(candidate, parent, plen) != 0) continue;
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

/* Resolve real CPIO symlinks, including relative targets and intermediate
 * directory links. No heap, no VFS, no writable state. All expansions are
 * bounded to the 256-byte pathname ABI and at most eight traversals.
 * Dot segments are applied AFTER each expansion, not to the link target
 * before locating the link (e.g. /bin/link/../file).
 */
int bootfs_resolve(const char *absolute, bool follow_final, char out[256]) {
    if (!mounted || !absolute || absolute[0] != '/') return -ENOENT;
    char pending[256];
    unsigned count = 0;
    while (absolute[count]) {
        if (count >= sizeof(pending) - 1) return -ENAMETOOLONG;
        pending[count] = absolute[count];
        count++;
    }
    pending[count] = 0;
    unsigned links = 0;
    for (;;) {
        char resolved[256];
        resolved[0] = '/';
        resolved[1] = 0;
        unsigned used = 1;
        unsigned pos = 0;
        bool restart = false;
        while (pending[pos]) {
            while (pending[pos] == '/') pos++;
            if (!pending[pos]) break;
            unsigned start = pos;
            while (pending[pos] && pending[pos] != '/') pos++;
            unsigned len = pos - start;
            if (len == 1 && pending[start] == '.') continue;
            if (len == 2 && pending[start] == '.' && pending[start + 1] == '.') {
                while (used > 1 && resolved[used - 1] != '/') used--;
                if (used > 1) used--;
                resolved[used] = 0;
                continue;
            }
            if (used + (used != 1) + len >= sizeof(resolved))
                return -ENAMETOOLONG;
            if (used != 1) resolved[used++] = '/';
            for (unsigned i = 0; i < len; i++)
                resolved[used++] = pending[start + i];
            resolved[used] = 0;
            const struct bootfs_entry *entry = bootfs_find(resolved);
            if (!entry) return -ENOENT;
            unsigned tail = pos;
            while (pending[tail] == '/') tail++;
            bool last = !pending[tail];
            if (bootfs_is_link(entry) && (!last || follow_final)) {
                if (++links > 8) return -ELOOP;
                char expanded[256];
                unsigned size = 0;
                if (entry->data[0] != '/') {
                    /* Relative links use the symlink's owning directory. */
                    unsigned parent = used - len;
                    if (parent > 1 && resolved[parent - 1] == '/') parent--;
                    for (unsigned i = 0; i < parent; i++)
                        expanded[size++] = resolved[i];
                    if (size > 1) expanded[size++] = '/';
                }
                if (size + entry->size >= sizeof(expanded))
                    return -ENAMETOOLONG;
                for (uint64_t i = 0; i < entry->size; i++)
                    expanded[size++] = (char)entry->data[i];
                for (unsigned i = pos; pending[i]; i++) {
                    if (size >= sizeof(expanded) - 1)
                        return -ENAMETOOLONG;
                    expanded[size++] = pending[i];
                }
                expanded[size] = 0;
                if (expanded[0] != '/') return -ENOENT;
                for (unsigned i = 0; i <= size; i++) pending[i] = expanded[i];
                restart = true;
                break;
            }
            if (!last && !bootfs_is_dir(entry)) return -ENOTDIR;
        }
        if (restart) continue;
        for (unsigned i = 0; i <= used; i++) out[i] = resolved[i];
        return 0;
    }
}
