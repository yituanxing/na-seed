#include "tmpfs.h"
#include "task.h"
#include "fd.h"
#include "bootfs.h"
#include "errno.h"
#include "lib/string.h"
#include "mm/frame.h"

/* RAM /tmp data and descriptor bindings are owned only by this module.
 * Linux syscall argument decoding and userspace copy remain in m1.c. */
static struct nas_tmp_file tmp_files[TMP_FILE_COUNT];
static uint8_t tmp_slot_owner[NAS_FD_LIMIT]; /* 1-based inode index */
static void tmp_try_reclaim(unsigned owner) {
    if (!owner || owner > TMP_FILE_COUNT) return;
    struct nas_tmp_file *node = &tmp_files[owner - 1];
    if (!node->used || node->linked) return;
    for (unsigned slot = 0; slot < NAS_FD_LIMIT; slot++)
        if (tmp_slot_owner[slot] == owner && nas_open_files[slot].refcount)
            return;
    tmp_truncate(node);
    node->used = false;
}


/* A single indirect frame contains 512 physical data-page addresses.
 * All storage comes from the shared native page-frame provider: no arch
 * switch, no reserved file-sized BSS arrays, no VFS-owned physical allocator.
 * A physical frame address of zero always denotes an absent page. */
static uint8_t *tmp_data_page(struct nas_tmp_file *file, uint64_t number,
                              bool create) {
    if (!file || number >= TMP_INDEX_PAGES * (uint64_t)TMP_PAGE_ENTRIES)
        return 0;
    unsigned outer = (unsigned)(number / TMP_PAGE_ENTRIES);
    unsigned inner = (unsigned)(number % TMP_PAGE_ENTRIES);
    uint64_t table_addr = file->index_pages[outer];
    if (!table_addr && create) {
        table_addr = nas_frame_alloc();
        if (!table_addr) return 0;
        file->index_pages[outer] = table_addr;
    }
    if (!table_addr) return 0;
    uint64_t *table = (uint64_t *)(uintptr_t)(nas_hhdm_offset() + table_addr);
    uint64_t data_addr = table[inner];
    if (!data_addr && create) {
        data_addr = nas_frame_alloc();
        if (!data_addr) return 0;
        table[inner] = data_addr;
    }
    return data_addr ? (uint8_t *)(uintptr_t)(nas_hhdm_offset() + data_addr) : 0;
}
int64_t tmp_read_at(const struct nas_tmp_file *const_file, uint64_t offset,
                    uint8_t *dst, uint64_t count) {
    if (!const_file || !const_file->used || !dst) return -EINVAL;
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
    }
    return (int64_t)done;
}
void tmp_truncate(struct nas_tmp_file *file) {
    if (!file) return;
    for (unsigned outer = 0; outer < TMP_INDEX_PAGES; outer++) {
        uint64_t frame = file->index_pages[outer];
        if (!frame) continue;
        uint64_t *table = (uint64_t *)(uintptr_t)(nas_hhdm_offset() + frame);
        for (unsigned inner = 0; inner < TMP_PAGE_ENTRIES; inner++) {
            if (table[inner]) {
                (void)nas_frame_free(table[inner]);
                table[inner] = 0;
            }
        }
        (void)nas_frame_free(frame);
        file->index_pages[outer] = 0;
    }
    file->size = 0;
}

/* Real CPIO /tmp mountpoint with a small volatile directory tree.
 * Unlinked entries keep their inode until every fork/dup FD releases it. */
bool tmp_path(const char *path) {
    return path[0]=='/' && path[1]=='t' && path[2]=='m' &&
           path[3]=='p' && path[4]=='/' && path[5];
}
bool tmp_parent(const char *path, char parent[256]) {
    if (!tmp_path(path)) return false;
    unsigned n = 0, slash = 0;
    for (; path[n] && n < 255; n++)
        if (path[n] == '/') slash = n;
    if (path[n] || slash < 4 || !path[slash+1]) return false;
    if (slash == 4) {
        parent[0]='/'; parent[1]='t'; parent[2]='m'; parent[3]='p';
        parent[4]=0;
    } else {
        for (unsigned i=0;i<slash;i++) parent[i]=path[i];
        parent[slash]=0;
    }
    return true;
}
struct nas_tmp_file *tmp_find(const char *path) {
    for (unsigned n=0;n<TMP_FILE_COUNT;n++)
        if (tmp_files[n].used && tmp_files[n].linked &&
            strcmp(tmp_files[n].path,path)==0) return &tmp_files[n];
    return 0;
}
int tmp_parent_check(const char *path) {
    char parent[256];
    if (!tmp_parent(path,parent)) return -EROFS;
    if (strcmp(parent,"/tmp")==0)
        return bootfs_is_dir(bootfs_find("/tmp")) ? 0 : -ENOENT;
    struct nas_tmp_file *dir=tmp_find(parent);
    return !dir ? -ENOENT : dir->is_dir ? 0 : -ENOTDIR;
}
struct nas_tmp_file *tmp_create(const char *path,bool dir) {
    for (unsigned n=0;n<TMP_FILE_COUNT;n++) {
        struct nas_tmp_file *file=&tmp_files[n];
        if (file->used) continue;
        unsigned i=0;
        while (path[i] && i<255) { file->path[i]=path[i]; i++; }
        file->path[i]=0;
        file->size=0;
        file->mode=dir ? 0040755U : 0100644U;
        file->is_dir=dir;
        file->linked=file->used=true;
        return file;
    }
    return 0;
}
unsigned tmp_number(const struct nas_tmp_file *file) {
    return (unsigned)(file-tmp_files);
}
const struct nas_tmp_file *tmp_child(const char *parent,unsigned index) {
    for (unsigned n=0;n<TMP_FILE_COUNT;n++) {
        const struct nas_tmp_file *file=&tmp_files[n];
        if (!file->used || !file->linked) continue;
        char dir[256];
        if (!tmp_parent(file->path,dir) || strcmp(dir,parent)!=0)
            continue;
        if (!index) return file;
        index--;
    }
    return 0;
}

void tmp_bind_slot(unsigned slot, const struct nas_tmp_file *file) {
    if (slot >= NAS_FD_LIMIT || !file) return;
    tmp_slot_owner[slot] = (uint8_t)(tmp_number(file) + 1);
}
struct nas_tmp_file *tmp_by_slot(unsigned slot) {
    if (slot >= NAS_FD_LIMIT) return 0;
    unsigned owner=tmp_slot_owner[slot];
    if (!owner || owner>TMP_FILE_COUNT) return 0;
    struct nas_tmp_file *file=&tmp_files[owner-1];
    return file->used ? file : 0;
}
void tmp_release_slot(unsigned slot) {
    if (slot >= NAS_FD_LIMIT) return;
    unsigned owner=tmp_slot_owner[slot];
    tmp_slot_owner[slot]=0;
    tmp_try_reclaim(owner);
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
    return 0;
}
int tmp_mkdir(const char *path, uint64_t mode) {
    if (bootfs_find(path) || tmp_find(path)) return -EEXIST;
    if (!tmp_path(path) || !bootfs_mounted()) return -EROFS;
    int parent=tmp_parent_check(path);
    if (parent) return parent;
    struct nas_tmp_file *dir=tmp_create(path,true);
    if (!dir) return -ENOSPC;
    dir->mode=0040000U | (uint32_t)(mode & 0777U);
    return 0;
}
int tmp_unlink(const char *path, uint64_t flags) {
    const uint64_t AT_REMOVEDIR=0x200ULL;
    if (flags & ~AT_REMOVEDIR) return -EINVAL;
    if (!tmp_path(path) || !bootfs_mounted()) return -EROFS;
    struct nas_tmp_file *file=tmp_find(path);
    if (!file) {
        int parent=tmp_parent_check(path);
        if (parent) return parent;
        return -ENOENT;
    }
    if (file->is_dir && !(flags & AT_REMOVEDIR)) return -EISDIR;
    if (!file->is_dir && (flags & AT_REMOVEDIR)) return -ENOTDIR;
    if (file->is_dir) {
        if (tmp_child(path,0)) return -ENOTEMPTY;
        for (unsigned n=0;n<NAS_TASK_LIMIT;n++) {
            struct nas_task *task=nas_task_at(n);
            if (task && task->state != NAS_TASK_FREE &&
                strcmp(task->cwd,path)==0) return -EBUSY;
        }
    }
    file->linked=false;
    tmp_try_reclaim(tmp_number(file)+1);
    return 0;
}
/* M6.9: an inode rename is a namespace operation; open descriptions keep
 * referencing the same inode regardless of the old/new pathname. A replaced
 * inode stays alive while inherited or duplicated descriptors hold it.
 *
 * Single-core cooperative syscall dispatch makes preflight+commit atomic.
 * No CPIO mutation or cross-mount fake copy/rename is permitted.
 */
static bool tmp_under(const char *path, const char *directory) {
    unsigned n = 0;
    while (directory[n]) {
        if (path[n] != directory[n]) return false;
        n++;
    }
    return path[n] == '/';
}
static bool tmp_move_name(char dest[256], const char *newbase,
                          const char *oldbase, const char *existing) {
    const unsigned base = strlen(newbase);
    const unsigned cut = strlen(oldbase);
    const unsigned suffix = strlen(existing + cut);
    if (base + suffix >= 256) return false;
    for (unsigned i = 0; i < base; i++) dest[i] = newbase[i];
    for (unsigned i = 0; i <= suffix; i++)
        dest[base + i] = existing[cut + i];
    return true;
}
int tmp_rename(const char *oldpath, const char *newpath, uint64_t flags) {
    /* Linux renameat2 RENAME_NOREPLACE only; others refuse execution. */
    if (flags & ~1ULL) return -EINVAL;
    const bool oldtmp = tmp_path(oldpath), newtmp = tmp_path(newpath);
    if (oldtmp != newtmp) return -EXDEV;
    if (!oldtmp || !bootfs_mounted()) return -EROFS;
    struct nas_tmp_file *source = tmp_find(oldpath);
    if (!source) {
        int parent = tmp_parent_check(oldpath);
        return parent ? parent : -ENOENT;
    }
    if (strcmp(oldpath, newpath)==0) return 0;
    int parent = tmp_parent_check(newpath);
    if (parent) return parent;
    if (source->is_dir && tmp_under(newpath, oldpath))
        return -EINVAL; /* cannot move a directory into itself */
    struct nas_tmp_file *target = tmp_find(newpath);
    if (target) {
        if (flags & 1ULL) return -EEXIST;
        if (source->is_dir && !target->is_dir)
            return -ENOTDIR;
        if (!source->is_dir && target->is_dir)
            return -EISDIR;
        if (target->is_dir && tmp_child(newpath, 0))
            return -ENOTEMPTY;
    }
    /* Check every descendant and task cwd before touching the namespace.
     * When moving directory trees, the cwd is an inode identity, so its
     * printable path must move too. All results fit fixed-sized buffers.
     */
    if (source->is_dir) {
        char candidate[256];
        for (unsigned n = 0; n < TMP_FILE_COUNT; n++) {
            const struct nas_tmp_file *entry = &tmp_files[n];
            if (!entry->used || !entry->linked ||
                !tmp_under(entry->path, oldpath)) continue;
            if (!tmp_move_name(candidate, newpath, oldpath, entry->path))
                return -ENAMETOOLONG;
        }
        for (unsigned n = 0; n < NAS_TASK_LIMIT; n++) {
            const struct nas_task *task = nas_task_at(n);
            if (task->state == NAS_TASK_FREE ||
                !tmp_under(task->cwd, oldpath)) continue;
            if (!tmp_move_name(candidate, newpath, oldpath, task->cwd))
                return -ENAMETOOLONG;
        }
    }
    /* Commit: remove destination pathname but preserve open inode contents. */
    if (target) {
        unsigned owner = tmp_number(target) + 1;
        target->linked = false;
        tmp_try_reclaim(owner);
    }
    if (source->is_dir) {
        char candidate[256];
        for (unsigned n = 0; n < TMP_FILE_COUNT; n++) {
            struct nas_tmp_file *entry = &tmp_files[n];
            if (!entry->used || !entry->linked ||
                !tmp_under(entry->path, oldpath)) continue;
            if (!tmp_move_name(candidate, newpath, oldpath, entry->path))
                return -ENAMETOOLONG; /* unreachable after preflight */
            unsigned i = 0;
            do { entry->path[i] = candidate[i]; } while (candidate[i++]);
        }
        for (unsigned n = 0; n < NAS_TASK_LIMIT; n++) {
            struct nas_task *task = nas_task_at(n);
            if (task->state == NAS_TASK_FREE ||
                !tmp_under(task->cwd, oldpath)) continue;
            if (!tmp_move_name(candidate, newpath, oldpath, task->cwd))
                return -ENAMETOOLONG; /* unreachable after preflight */
            unsigned i = 0;
            do { task->cwd[i] = candidate[i]; } while (candidate[i++]);
        }
    }
    unsigned i = 0;
    do { source->path[i] = newpath[i]; } while (newpath[i++]);
    return 0;
}
