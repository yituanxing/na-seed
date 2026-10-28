#include "tmpfs.h"
#include "task.h"
#include "fd.h"
#include "fs/vfs.h"
#include "bootfs.h"
#include "errno.h"
#include "lib/string.h"
#include "mm/frame.h"
#include "time/clock.h"
#include "arch/arch.h"

/* Metadata is itself demand-allocated from the common frame provider:
 * one page holds several reusable inode records; a linked slab chain grows
 * as large as available RAM allows instead of stopping after eight files.
 * Crucially, pointers remain stable across growth and fork/dup/rename.
 * Only the generic tmpfs owns slabs; no architecture-dependent allocator. */
#define TMP_INODES_PER_SLAB ((4096U - sizeof(void *)) / sizeof(struct nas_tmp_file))
struct tmp_inode_slab {
    struct tmp_inode_slab *next;
    struct nas_tmp_file nodes[TMP_INODES_PER_SLAB];
};
_Static_assert(sizeof(struct tmp_inode_slab) <= 4096, "tmp inode slab must fit one frame");
_Static_assert(TMP_INODES_PER_SLAB >= 2, "tmp inode must fit slab");

/* Use the existing cross-ISA RTC-anchored timekeeper; if firmware has
 * no valid RTC, monotonic nanoseconds still order writes within this boot.
 * Never use synthetic always-equal timestamps for real GNU Make. */
static uint64_t tmp_now_ns(void) {
    uint64_t now = 0;
    return nas_time_realtime_ns(&now) ? now : nas_arch_monotonic_ns();
}
static uint64_t tmp_next_mtime(uint64_t previous) {
    uint64_t now = tmp_now_ns();
    return now > previous ? now : previous + (previous < UINT64_MAX);
}
static struct tmp_inode_slab *tmp_slabs;
static struct nas_tmp_file *tmp_slot_owner[NAS_OPEN_FILE_LIMIT];
static uint64_t tmp_next_ino = 1;

static struct tmp_inode_slab *tmp_new_slab(void) {
    uint64_t frame = nas_frame_alloc();
    if (!frame) return 0;
    struct tmp_inode_slab *slab =
        (struct tmp_inode_slab *)(uintptr_t)(nas_hhdm_offset() + frame);
    memset(slab,0,4096);
    slab->next = tmp_slabs;
    tmp_slabs = slab;
    return slab;
}
static void tmp_try_reclaim(struct nas_tmp_file *node) {
    if (!node || !node->used || node->nlink) return;
    /* An orphan inode is also owned by generic VFS File and FS cwd pins.
     * Legacy tmp_slot_owner alone does NOT cover cwd, independently opened
     * mounted nodes or a future mmap-held VFS reference. */
    if(nas_vfs_inode_is_pinned(node))return;
    for (unsigned slot = 0; slot < NAS_OPEN_FILE_LIMIT; slot++)
        if (tmp_slot_owner[slot] == node && nas_open_files[slot].refcount)
            return;
    tmp_truncate(node);
    node->used = false;
}


/* An optional root frame indexes 512 indirect pages; each indirect page
 * indexes 512 independently allocated 4-KiB file data pages. The common
 * frame allocator and HHDM supply all storage; no ISA-specific fast path. */
static uint8_t *tmp_data_page(struct nas_tmp_file *file, uint64_t number,
                              bool create) {
    if (!file || number >= TMP_INDEX_ROOT_ENTRIES * (uint64_t)TMP_PAGE_ENTRIES)
        return 0;
    if (!file->index_root) {
        if (!create) return 0;
        uint64_t root_addr = nas_frame_alloc();
        if (!root_addr) return 0;
        memset((void *)(uintptr_t)(nas_hhdm_offset() + root_addr),
               0, (size_t)TMP_PAGE_SIZE);
        file->index_root = root_addr;
    }
    uint64_t *root = (uint64_t *)(uintptr_t)(
        nas_hhdm_offset() + file->index_root);
    unsigned outer = (unsigned)(number / TMP_PAGE_ENTRIES);
    unsigned inner = (unsigned)(number % TMP_PAGE_ENTRIES);
    uint64_t table_addr = root[outer];
    if (!table_addr && create) {
        table_addr = nas_frame_alloc();
        if (!table_addr) return 0;
        memset((void *)(uintptr_t)(nas_hhdm_offset() + table_addr),
               0, (size_t)TMP_PAGE_SIZE);
        root[outer] = table_addr;
    }
    if (!table_addr) return 0;
    uint64_t *table = (uint64_t *)(uintptr_t)(nas_hhdm_offset() + table_addr);
    uint64_t data_addr = table[inner];
    if (!data_addr && create) {
        data_addr = nas_frame_alloc();
        if (!data_addr) return 0;
        memset((void *)(uintptr_t)(nas_hhdm_offset() + data_addr),
               0, (size_t)TMP_PAGE_SIZE);
        table[inner] = data_addr;
    }
    return data_addr ? (uint8_t *)(uintptr_t)(nas_hhdm_offset() + data_addr) : 0;
}
int64_t tmp_read_at(const struct nas_tmp_file *const_file, uint64_t offset,
                    uint8_t *dst, uint64_t count) {
    if (!const_file || !const_file->used || (!dst && count)) return -EINVAL;
    /* A zero-byte read validates a regular inode without requiring a
     * destination buffer. In particular, MAP_PRIVATE validates its file
     * description with nas_fs_mmap_read(fd, offset, NULL, 0), even when
     * the underlying file is a writable tmpfs inode (Git config/index). */
    if (!count) return 0;
    if (offset >= const_file->size) return 0;
    if (count > const_file->size - offset) count = const_file->size - offset;
    uint64_t done = 0;
    /* A read does not allocate physical pages for sparse zero-filled holes. */
    struct nas_tmp_file *file = (struct nas_tmp_file *)(uintptr_t)const_file;
    while (done < count) {
        uint64_t at = offset + done;
        uint64_t length = TMP_PAGE_SIZE - at % TMP_PAGE_SIZE;
        if (length > count - done) length = count - done;
        uint8_t *page = tmp_data_page(file, at / TMP_PAGE_SIZE, false);
        if (page) memcpy(dst + done, page + at % TMP_PAGE_SIZE, (size_t)length);
        else memset(dst + done, 0, (size_t)length);
        done += length;
    }
    return (int64_t)done;
}
int64_t tmp_write_at(struct nas_tmp_file *file, uint64_t offset,
                     const uint8_t *src, uint64_t count) {
    if (!file || !file->used || !src || file->is_dir) return -EINVAL;
    if (!count) return 0;
    if (offset >= TMP_FILE_CAP) return -ENOSPC;
    if (count > TMP_FILE_CAP - offset) count = TMP_FILE_CAP - offset;
    uint64_t done = 0;
    while (done < count) {
        uint64_t at = offset + done;
        uint64_t length = TMP_PAGE_SIZE - at % TMP_PAGE_SIZE;
        if (length > count - done) length = count - done;
        uint8_t *page = tmp_data_page(file, at / TMP_PAGE_SIZE, true);
        if (!page) return done ? (int64_t)done : -ENOMEM;
        memcpy(page + at % TMP_PAGE_SIZE, src + done, (size_t)length);
        done += length;
        if (at + length > file->size) file->size = at + length;
        file->mtime_ns = tmp_next_mtime(file->mtime_ns);
        file->ctime_ns = file->mtime_ns;
    }
    return (int64_t)done;
}
void tmp_truncate(struct nas_tmp_file *file) {
    if (!file) return;
    if (file->index_root) {
        uint64_t *root = (uint64_t *)(uintptr_t)(
            nas_hhdm_offset() + file->index_root);
        for (unsigned outer = 0; outer < TMP_INDEX_ROOT_ENTRIES; outer++) {
            uint64_t frame = root[outer];
            if (!frame) continue;
            uint64_t *table = (uint64_t *)(uintptr_t)(nas_hhdm_offset() + frame);
            for (unsigned inner = 0; inner < TMP_PAGE_ENTRIES; inner++) {
                if (table[inner]) (void)nas_frame_put(table[inner]);
            }
            (void)nas_frame_put(frame);
        }
        (void)nas_frame_put(file->index_root);
        file->index_root = 0;
    }
    file->size = 0;
    if (file->used) {
        file->mtime_ns = tmp_next_mtime(file->mtime_ns);
        file->ctime_ns = file->mtime_ns;
    }
}
/* Linux ftruncate: grow as sparse zero-filled pages, shrink discards pages
 * AND zeroes the partial final page, so later regrowth cannot expose
 * truncated bytes. Inode identity and open-descriptor offset survive. */
int tmp_resize(struct nas_tmp_file *file, uint64_t size) {
    if (!file || !file->used) return -ENOENT;
    if (file->is_dir) return -EISDIR;
    if (size > TMP_FILE_CAP) return -EFBIG;
    uint64_t old = file->size;
    if (size == old) return 0;
    if (size < old) {
        if (size % TMP_PAGE_SIZE) {
            uint8_t *last = tmp_data_page(file, size / TMP_PAGE_SIZE, false);
            if (last) memset(last + size % TMP_PAGE_SIZE, 0,
                             (size_t)(TMP_PAGE_SIZE - size % TMP_PAGE_SIZE));
        }
        if (file->index_root) {
            uint64_t *root = (uint64_t *)(uintptr_t)(
                nas_hhdm_offset() + file->index_root);
            uint64_t first = (size + TMP_PAGE_SIZE - 1) / TMP_PAGE_SIZE;
            uint64_t oldpages = (old + TMP_PAGE_SIZE - 1) / TMP_PAGE_SIZE;
            for (uint64_t page = first; page < oldpages; page++) {
                unsigned outer = (unsigned)(page / TMP_PAGE_ENTRIES);
                uint64_t frame = root[outer];
                if (!frame) continue;
                uint64_t *slots = (uint64_t *)(uintptr_t)(
                    nas_hhdm_offset() + frame);
                unsigned inner = (unsigned)(page % TMP_PAGE_ENTRIES);
                if (slots[inner]) {
                    (void)nas_frame_put(slots[inner]);
                    slots[inner] = 0;
                }
            }
            bool root_has_index = false;
            for (unsigned outer = 0; outer < TMP_INDEX_ROOT_ENTRIES; outer++) {
                uint64_t frame = root[outer];
                if (!frame) continue;
                uint64_t *slots = (uint64_t *)(uintptr_t)(
                    nas_hhdm_offset() + frame);
                bool any = false;
                for (unsigned i = 0; i < TMP_PAGE_ENTRIES; i++)
                    if (slots[i]) { any = true; break; }
                if (!any) {
                    (void)nas_frame_put(frame);
                    root[outer] = 0;
                } else root_has_index = true;
            }
            if (!root_has_index) {
                (void)nas_frame_put(file->index_root);
                file->index_root = 0;
            }
        }
    }
    file->size = size;
    file->mtime_ns = tmp_next_mtime(file->mtime_ns);
    file->ctime_ns = file->mtime_ns;
    return 0;
}
int tmp_utimens(struct nas_tmp_file *file, uint64_t access_ns,
                uint64_t modified_ns, bool set_access, bool set_modified) {
    if (!file || !file->used) return -ENOENT;
    if (set_access) file->atime_ns = access_ns;
    if (set_modified) file->mtime_ns = modified_ns;
    if (set_access || set_modified) file->ctime_ns =
        tmp_next_mtime(file->ctime_ns);
    return 0;
}

/* Path-to-inode bindings live in fs/tmp_namespace.c. */
bool tmp_is_link(const struct nas_tmp_file *file) {
    return file && (file->mode & 0170000U) == 0120000U;
}
/* A volatile symlink is a real tmpfs inode. Its literal target (including
 * relative path segments) is stored in the same bounded sparse-page data
 * backend as file data, so rename/unlink follow existing inode ownership. */
int tmp_link_target(const struct nas_tmp_file *file, char target[256]) {
    if (!tmp_is_link(file)) return -EINVAL;
    if (file->size >= 256) return -ENAMETOOLONG;
    int64_t copied = tmp_read_at(file, 0, (uint8_t *)target, file->size);
    if (copied < 0) return (int)copied;
    if ((uint64_t)copied != file->size) return -EIO;
    target[file->size] = 0;
    return 0;
}
/* Namespace owns symlink names and writable inode creation. */
/* Inode storage identity is independent from namespace name/link count.
 * The namespace owner publishes a dentry only AFTER successful allocation. */
struct nas_tmp_file *tmp_inode_alloc(bool dir,uint32_t mode) {
    struct nas_tmp_file *file=0;
    for (struct tmp_inode_slab *slab=tmp_slabs;slab && !file;slab=slab->next)
        for(unsigned n=0;n<TMP_INODES_PER_SLAB;n++)
            if(!slab->nodes[n].used) {file=&slab->nodes[n];break;}
    if(!file) {
        struct tmp_inode_slab *slab=tmp_new_slab();
        if(!slab)return 0;
        file=&slab->nodes[0];
    }
    *file=(struct nas_tmp_file){0};
    file->used=true;
    file->ino=tmp_next_ino++;
    file->mode=(dir?0040000U:0100000U)|(mode&07777U);
    file->is_dir=dir;
    file->atime_ns=file->mtime_ns=file->ctime_ns=tmp_now_ns();
    return file;
}
void tmp_inode_link(struct nas_tmp_file *file) {
    if(!file || !file->used)return;
    file->nlink++;
    file->ctime_ns=tmp_next_mtime(file->ctime_ns);
}
void tmp_inode_unlink(struct nas_tmp_file *file) {
    if(!file || !file->used || !file->nlink)return;
    file->nlink--;
    file->ctime_ns=tmp_next_mtime(file->ctime_ns);
    tmp_try_reclaim(file);
}
void tmp_inode_discard(struct nas_tmp_file *file) {
    tmp_try_reclaim(file);
}
void tmp_inode_touch(struct nas_tmp_file *file) {
    if(file && file->used)file->ctime_ns=tmp_next_mtime(file->ctime_ns);
}
uint64_t tmp_number(const struct nas_tmp_file *file) {
    return file ? file->ino : 0;
}
/* Directory entries live in fs/tmp_namespace.c. */
void tmp_bind_slot(unsigned slot, const struct nas_tmp_file *file) {
    if (slot >= NAS_OPEN_FILE_LIMIT || !file) return;
    tmp_slot_owner[slot] = (struct nas_tmp_file *)(uintptr_t)file;
}
struct nas_tmp_file *tmp_by_slot(unsigned slot) {
    if (slot >= NAS_OPEN_FILE_LIMIT) return 0;
    struct nas_tmp_file *file=tmp_slot_owner[slot];
    return file && file->used ? file : 0;
}
void tmp_release_slot(unsigned slot) {
    if (slot >= NAS_OPEN_FILE_LIMIT) return;
    struct nas_tmp_file *file=tmp_slot_owner[slot];
    tmp_slot_owner[slot]=0;
    tmp_try_reclaim(file);
}
/* These are actual Linux mkdir(83), rmdir(84), unlink(87) and
 * mkdirat/unlinkat entry points, not shell-command path rewrites.
 * Read-only CPIO refuses mutations; only the /tmp overlay is writable.
 */
/* Inode mode is shared by chmod/fchmod, access and execve.
 * Preserve the file-type bits; CPIO remains strictly read-only. */
int tmp_chmod(struct nas_tmp_file *file, uint64_t mode) {
    if(!file || !file->used)return -ENOENT;
    file->mode=(file->mode & 0170000U) | (uint32_t)(mode & 07777U);
    file->ctime_ns=tmp_next_mtime(file->ctime_ns);
    return 0;
}
/* Namespace mutations are implemented by fs/tmp_namespace.c. */
