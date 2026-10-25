#include "bootfs.h"
#include "tmpfs.h"
#include "errno.h"
#include "lib/string.h"
#include "mm/frame.h"
#include <stddef.h>

#define BOOTFS_BUCKETS 8192U
/* Explicit resource upper limit, no small fixed array of input files. */
#define BOOTFS_ENTRY_LIMIT 65536U
#define NEWC_HEADER 110ULL
#define S_IFMT 0170000U
#define S_IFREG 0100000U
#define S_IFDIR 0040000U
#define S_IFLNK 0120000U

/* One frame per slab: stable entry pointers across grow, fork and lookup.
 * The archive bytes themselves remain zero-copy and strictly read-only. */
#define BOOTFS_ENTRIES_PER_SLAB \
    ((4096U - sizeof(void *) - sizeof(unsigned)) / sizeof(struct bootfs_entry))
struct bootfs_slab {
    struct bootfs_slab *next;
    unsigned used;
    struct bootfs_entry entries[BOOTFS_ENTRIES_PER_SLAB];
};
_Static_assert(sizeof(struct bootfs_slab) <= 4096, "bootfs slab fits one page");
_Static_assert(BOOTFS_ENTRIES_PER_SLAB > 0, "bootfs entry must fit one page");

static struct bootfs_slab *slab_head, *slab_tail;
static struct bootfs_entry *buckets[BOOTFS_BUCKETS];
static struct bootfs_entry *hardlinks[BOOTFS_BUCKETS];
static unsigned entry_count;
static bool mounted;

static unsigned bootfs_bucket(const char *path) {
    uint32_t hash = 2166136261U;
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p)
        hash = (hash ^ *p) * 16777619U;
    return hash & (BOOTFS_BUCKETS - 1);
}
/* Only regular files participate in the separate hardlink table.
 * Directories also report nlink>1 and MUST NOT be coalesced. */
static unsigned hardlink_bucket(uint32_t ino,uint32_t major,uint32_t minor) {
    uint32_t key=(ino*16777619U) ^ (major*2246822519U) ^
                 (minor*3266489917U);
    return key & (BOOTFS_BUCKETS-1);
}
static struct bootfs_entry *hardlink_find(uint32_t ino,
                                          uint32_t major,uint32_t minor) {
    unsigned key=hardlink_bucket(ino,major,minor);
    for (struct bootfs_entry *e=hardlinks[key];e;e=e->hardlink_next)
        if (e->cpio_ino==ino && e->cpio_major==major &&
            e->cpio_minor==minor) return e;
    return 0;
}
/* GNU newc stores each hardlinked regular file's content once.
 * Subsequent/previous pathname records can have filesize=0. Fixup after
 * the trailer keeps lookups O(1) without duplicating immutable bytes. */
/* Resolve all parent directories only AFTER parsing: CPIO order is not a
 * directory-tree order. Hash lookup yields O(1) expected parent resolution.
 * Child insertion follows original archive order, preserving old readdir.
 * Missing parents are deliberately left unlinked as in the old scan path. */
static void bootfs_index_children(void) {
    for (struct bootfs_slab *slab=slab_head;slab;slab=slab->next)
        for (unsigned i=0;i<slab->used;i++) {
            struct bootfs_entry *e=&slab->entries[i];
            e->first_child=e->last_child=e->next_sibling=0;
        }
    for (struct bootfs_slab *slab=slab_head;slab;slab=slab->next)
        for (unsigned i=0;i<slab->used;i++) {
            struct bootfs_entry *e=&slab->entries[i];
            if (strcmp(e->path,"/")==0) continue;
            char parent[256];
            unsigned end=0;
            while (e->path[end]) end++;
            while (end>0 && e->path[end-1]!='/') end--;
            if (end==0) continue;
            unsigned len=end==1?1:end-1;
            for (unsigned j=0;j<len;j++)parent[j]=e->path[j];
            parent[len]=0;
            struct bootfs_entry *dir=(struct bootfs_entry*)bootfs_find(parent);
            if (!dir || !bootfs_is_dir(dir)) continue;
            if (dir->last_child)dir->last_child->next_sibling=e;
            else dir->first_child=e;
            dir->last_child=e;
        }
}

static void bootfs_finish_hardlinks(void) {
    for (struct bootfs_slab *slab=slab_head;slab;slab=slab->next)
        for (unsigned i=0;i<slab->used;i++) {
            struct bootfs_entry *e=&slab->entries[i];
            if (!e->hardlink_owner) continue;
            const struct bootfs_entry *owner=e->hardlink_owner;
            e->data=owner->data;
            e->size=owner->size;
            e->ino=owner->ino;
        }
}
static void bootfs_reset(void) {
    struct bootfs_slab *slab = slab_head;
    while (slab) {
        struct bootfs_slab *next = slab->next;
        (void)nas_frame_free((uint64_t)(uintptr_t)slab - nas_hhdm_offset());
        slab = next;
    }
    slab_head = slab_tail = 0;
    for (unsigned i = 0; i < BOOTFS_BUCKETS; ++i) {
        buckets[i] = 0;
        hardlinks[i] = 0;
    }
    entry_count = 0;
    mounted = false;
}
static struct bootfs_entry *bootfs_new_entry(void) {
    if (entry_count >= BOOTFS_ENTRY_LIMIT) return 0;
    if (!slab_tail || slab_tail->used == BOOTFS_ENTRIES_PER_SLAB) {
        uint64_t physical = nas_frame_alloc();
        if (!physical) return 0;
        struct bootfs_slab *slab =
            (struct bootfs_slab *)(uintptr_t)(physical + nas_hhdm_offset());
        slab->next = 0;
        slab->used = 0;
        if (slab_tail) slab_tail->next = slab;
        else slab_head = slab;
        slab_tail = slab;
    }
    return &slab_tail->entries[slab_tail->used++];
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
    bootfs_reset();
    if (!bytes || size < NEWC_HEADER) return false;
    const uint8_t *base = (const uint8_t *)bytes;
    uint64_t at = 0;
    while (at < size) {
        if (size - at < NEWC_HEADER) return false;
        const uint8_t *h = base + at;
        if (memcmp(h, "070701", 6) != 0) return false;
        uint32_t file_size, name_size, mode, mtime;
        uint32_t ino,nlink,major,minor;
        if (!hex8(h + 54, &file_size) ||
            !hex8(h + 94, &name_size) ||
            !hex8(h + 14, &mode) ||
            !hex8(h + 46, &mtime) ||
            !hex8(h + 6, &ino) ||
            !hex8(h + 38, &nlink) ||
            !hex8(h + 62, &major) ||
            !hex8(h + 70, &minor) ||
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
            bootfs_finish_hardlinks();
            bootfs_index_children();
            mounted = entry_count > 0 && bootfs_find("/") != 0;
            return mounted;
        }
        unsigned type = mode & S_IFMT;
        if (type != S_IFREG && type != S_IFDIR && type != S_IFLNK) return false;
        if (type == S_IFDIR && file_size) return false;
        /* In newc, a symlink's bytes are its literal target without NUL. */
        if (type == S_IFLNK) {
            if (!file_size || file_size >= 256) return false;
            for (uint32_t n = 0; n < file_size; n++)
                if (base[data_at + n] == 0) return false;
        }
        struct bootfs_entry *entry = bootfs_new_entry();
        if (!entry || !archive_name(entry, name, name_size) ||
            bootfs_find(entry->path)) return false;
        entry->mode = mode;
        entry->mtime_sec = mtime;
        entry->size = file_size;
        entry->data = base + data_at;
        entry->ino = entry_count + 1;
        entry->cpio_ino = ino;
        entry->cpio_major = major;
        entry->cpio_minor = minor;
        entry->nlink = nlink ? nlink : 1;
        entry->hardlink_owner = 0;
        entry->hardlink_next = 0;
        if (type == S_IFREG && nlink > 1) {
            struct bootfs_entry *owner=hardlink_find(ino,major,minor);
            if (!owner) {
                unsigned key=hardlink_bucket(ino,major,minor);
                entry->hardlink_owner=entry;
                entry->hardlink_next=hardlinks[key];
                hardlinks[key]=entry;
            } else {
                /* A single inode's metadata must agree. Multiple nonempty
                 * data records are ambiguous: reject malformed archives. */
                if (owner->mode != mode || owner->nlink != nlink ||
                    owner->mtime_sec != mtime ||
                    (file_size && owner->size)) return false;
                entry->hardlink_owner=owner;
                if (file_size) {
                    owner->data=entry->data;
                    owner->size=file_size;
                }
            }
        }
        unsigned bucket = bootfs_bucket(entry->path);
        entry->hash_next = buckets[bucket];
        buckets[bucket] = entry;
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
    for (struct bootfs_entry *entry = buckets[bootfs_bucket(absolute)];
         entry; entry = entry->hash_next)
        if (strcmp(entry->path, absolute) == 0) return entry;
    return 0;
}
bool bootfs_is_dir(const struct bootfs_entry *entry) {
    return entry && (entry->mode & S_IFMT) == S_IFDIR;
}
const struct bootfs_entry *bootfs_child(const struct bootfs_entry *dir,
                                        unsigned ordinal) {
    if (!bootfs_is_dir(dir)) return 0;
    const struct bootfs_entry *item=dir->first_child;
    while (item && ordinal) {
        item=item->next_sibling;
        ordinal--;
    }
    return item;
}

/* Resolve real CPIO and volatile /tmp symlinks in one pathwalk, including
 * relative targets and intermediate directory links. No heap and no fake
 * namespace. All expansions are bounded to 256 bytes and eight traversals.
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
            const struct nas_tmp_file *tmp = entry ? 0 : tmp_find(resolved);
            if (!entry && !tmp) return -ENOENT;
            unsigned tail = pos;
            while (pending[tail] == '/') tail++;
            bool last = !pending[tail];
            if ((bootfs_is_link(entry) || tmp_is_link(tmp)) &&
                (!last || follow_final)) {
                if (++links > 8) return -ELOOP;
                char expanded[256];
                char volatile_target[256];
                const uint8_t *target=entry ? entry->data : (const uint8_t *)volatile_target;
                uint64_t target_length=entry ? entry->size : tmp->size;
                if (tmp) {
                    int error=tmp_link_target(tmp,volatile_target);
                    if (error) return error;
                }
                unsigned size = 0;
                if (target[0] != '/') {
                    /* Relative links use the symlink's owning directory. */
                    unsigned parent = used - len;
                    if (parent > 1 && resolved[parent - 1] == '/') parent--;
                    for (unsigned i = 0; i < parent; i++)
                        expanded[size++] = resolved[i];
                    if (size > 1) expanded[size++] = '/';
                }
                if (size + target_length >= sizeof(expanded))
                    return -ENAMETOOLONG;
                for (uint64_t i = 0; i < target_length; i++)
                    expanded[size++] = (char)target[i];
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
            if (!last && !(entry ? bootfs_is_dir(entry) : tmp->is_dir))
                return -ENOTDIR;
        }
        if (restart) continue;
        for (unsigned i = 0; i <= used; i++) out[i] = resolved[i];
        return 0;
    }
}
