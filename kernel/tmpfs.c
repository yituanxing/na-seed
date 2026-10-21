#include "tmpfs.h"
#include "task.h"
#include "bootfs.h"
#include "errno.h"

/* RAM /tmp data and descriptor bindings are owned only by this module.
 * Linux syscall argument decoding and userspace copy remain in m1.c. */
static struct nas_tmp_file tmp_files[TMP_FILE_COUNT];
static uint8_t tmp_slot_owner[NAS_FD_LIMIT]; /* 1-based inode index */
static bool same_string(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}
static void tmp_try_reclaim(unsigned owner) {
    if (!owner || owner > TMP_FILE_COUNT) return;
    struct nas_tmp_file *node = &tmp_files[owner - 1];
    if (!node->used || node->linked) return;
    for (unsigned slot = 0; slot < NAS_FD_LIMIT; slot++)
        if (tmp_slot_owner[slot] == owner && nas_file_refcount[slot])
            return;
    node->used = false;
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
            same_string(tmp_files[n].path,path)) return &tmp_files[n];
    return 0;
}
int tmp_parent_check(const char *path) {
    char parent[256];
    if (!tmp_parent(path,parent)) return -EROFS;
    if (same_string(parent,"/tmp"))
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
        if (!tmp_parent(file->path,dir) || !same_string(dir,parent))
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
                same_string(task->cwd,path)) return -EBUSY;
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
static unsigned tmp_strlen(const char *text) {
    unsigned n = 0;
    while (text[n]) n++;
    return n;
}
static bool tmp_move_name(char dest[256], const char *newbase,
                          const char *oldbase, const char *existing) {
    const unsigned base = tmp_strlen(newbase);
    const unsigned cut = tmp_strlen(oldbase);
    const unsigned suffix = tmp_strlen(existing + cut);
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
    if (same_string(oldpath, newpath)) return 0;
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

