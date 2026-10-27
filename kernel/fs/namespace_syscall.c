/* Pathname resolution and writable namespace Linux ABI adapter.
 * The same path normalization and mount-limited CPIO/tmpfs model is used
 * for every ISA. This is not yet a mount-aware or dentry-based VFS. */
#include <stdint.h>
#include <stdbool.h>
#include "../errno.h"
#include "../task.h"
#include "../fd.h"
#include "../bootfs.h"
#include "../tmpfs.h"
#include "../path.h"
#include "../lib/string.h"
#include "../mm/usercopy.h"
#include "../arch/mmu.h"
#include "mode.h"
#include "syscall.h"
#include "lookup.h"
#include "procfs.h"
#include "namespace_syscall.h"
#define TASK (*nas_current_task)
#define current_workdir TASK.fs->cwd

/* Lexically combine absolute, cwd-relative and dirfd-relative paths.
 * bootfs_resolve performs archive symlink traversal at the actual lookup.
 */
/* This adapter owns user memory and dirfd lookup. Component processing
 * lives in path.c and can be tested independently of the syscall ABI. */
uint64_t nas_fs_resolve_path(uint64_t dirfd, uint64_t pathname,
                             char out[256]) {
    char src[256];
    unsigned n=0;
    if (pathname>=USER_LIMIT) return (uint64_t)-EFAULT;
    for (;n<sizeof(src);n++) {
        uint8_t *p=nas_mmu_user_byte(pathname+n);
        if (!p) return (uint64_t)-EFAULT;
        src[n]=(char)*p;
        if (!src[n]) break;
    }
    if (n==sizeof(src)) return (uint64_t)-ENAMETOOLONG;
    if (!n) return (uint64_t)-ENOENT;
    const char *base=0;
    char proc_base[256];
    if (src[0]!='/') {
        if (dirfd==(uint64_t)-100) base=current_workdir;
        else {
            if (dirfd>=NAS_FD_LIMIT || TASK.fds->entries[dirfd].kind==FD_CLOSED)
                return (uint64_t)-EBADF;
            if (TASK.fds->entries[dirfd].kind==FD_PROC_DIR) {
                unsigned slot=TASK.fds->entries[dirfd].slot;
                struct nas_proc_node node={nas_open_files[slot].proc_kind,
                                           nas_open_files[slot].proc_pid,
                                           nas_open_files[slot].proc_tgid,
                                           nas_open_files[slot].proc_fd};
                if(!nas_proc_dir_path(node,proc_base))return (uint64_t)-ENOENT;
                base=proc_base;
            } else if (TASK.fds->entries[dirfd].kind==FD_TMP_DIR) {
                const struct nas_tmp_file *dir=tmp_by_slot(TASK.fds->entries[dirfd].slot);
                if (!dir) return (uint64_t)-EBADF;
                base=tmp_name(dir);
                if(!base)return (uint64_t)-ENOENT;
            } else {
                const struct nas_open_file *opened=
                    &nas_open_files[TASK.fds->entries[dirfd].slot];
                if(TASK.fds->entries[dirfd].kind==FD_DIR &&
                   !opened->boot_entry && opened->vnode_pinned &&
                   opened->vnode.is_dir && opened->origin_path[0]) {
                    base=opened->origin_path;
                } else {
                if (TASK.fds->entries[dirfd].kind!=FD_DIR || !opened->boot_entry)
                    return (uint64_t)-ENOTDIR;
                base=opened->boot_entry->path;
                }
            }
        }
    }
    return (uint64_t)(int64_t)nas_path_resolve(base,src,out);
}

/* Linux syscall decoders retain user-pointer validation and task-relative
 * path resolution; the RAM filesystem backend owns namespace operations. */
static uint64_t sys_mkdirat_tmp(uint64_t dirfd, uint64_t pathname,
                                uint64_t mode) {
    char path[256];
    uint64_t ret = nas_fs_resolve_path(dirfd, pathname, path);
    return (int64_t)ret < 0 ? ret :
        (uint64_t)(int64_t)tmp_mkdir(path,
                            nas_fs_create_mode(mode,TASK.fs->umask));
}
static uint64_t sys_unlinkat_tmp(uint64_t dirfd, uint64_t pathname,
                                 uint64_t flags) {
    char path[256];
    uint64_t ret = nas_fs_resolve_path(dirfd, pathname, path);
    return (int64_t)ret < 0 ? ret :
        (uint64_t)(int64_t)tmp_unlink(path, flags);
}
static uint64_t sys_renameat_tmp(uint64_t olddirfd, uint64_t oldptr,
                                 uint64_t newdirfd, uint64_t newptr,
                                 uint64_t flags) {
    /* Reject unsupported flags before reading user paths, as before. */
    if (flags & ~1ULL) return (uint64_t)-EINVAL;
    char oldpath[256], newpath[256];
    uint64_t ret = nas_fs_resolve_path(olddirfd, oldptr, oldpath);
    if ((int64_t)ret < 0) return ret;
    ret = nas_fs_resolve_path(newdirfd, newptr, newpath);
    return (int64_t)ret < 0 ? ret :
        (uint64_t)(int64_t)tmp_rename(oldpath, newpath, flags);
}
static uint64_t sys_symlinkat_tmp(uint64_t targetptr,uint64_t dirfd,
                                  uint64_t linkptr) {
    char target[256], linkpath[256];
    if (targetptr>=USER_LIMIT) return (uint64_t)-EFAULT;
    unsigned i=0;
    for (;i<sizeof(target);i++) {
        uint8_t *p=nas_mmu_user_byte(targetptr+i);
        if (!p) return (uint64_t)-EFAULT;
        target[i]=(char)*p;
        if (!target[i]) break;
    }
    if (i==sizeof(target)) return (uint64_t)-ENAMETOOLONG;
    uint64_t ret=nas_fs_resolve_path(dirfd,linkptr,linkpath);
    return (int64_t)ret<0 ? ret :
        (uint64_t)(int64_t)tmp_symlink(target,linkpath);
}

/* Hardlinks reuse the same tmpfs inode: no copies, no fake CPIO writes.
 * AT_SYMLINK_FOLLOW applies only to the old endpoint of linkat(). */
static uint64_t sys_linkat_tmp(uint64_t olddirfd,uint64_t oldptr,
                               uint64_t newdirfd,uint64_t newptr,
                               uint64_t flags) {
    const uint64_t AT_SYMLINK_FOLLOW=0x400ULL;
    if(flags & ~AT_SYMLINK_FOLLOW)return (uint64_t)-EINVAL;
    char oldpath[256],newpath[256];
    uint64_t ret=nas_fs_resolve_path(olddirfd,oldptr,oldpath);
    if((int64_t)ret<0)return ret;
    if(flags & AT_SYMLINK_FOLLOW) {
        char actual[256];
        int err=nas_fs_resolve_existing(oldpath,true,actual);
        if(err)return (uint64_t)(int64_t)err;
        unsigned i=0;
        do{oldpath[i]=actual[i];}while(actual[i++]);
    }
    ret=nas_fs_resolve_path(newdirfd,newptr,newpath);
    return (int64_t)ret<0 ? ret :
        (uint64_t)(int64_t)tmp_link(oldpath,newpath);
}

static uint64_t sys_getcwd_root(uint64_t buffer, uint64_t size) {
    uint64_t length = 0;
    while (current_workdir[length]) length++;
    length++; /* Linux raw getcwd includes NUL in returned length. */
    if (size < length) return (uint64_t)-ERANGE;
    return nas_user_write_bytes(buffer, (const uint8_t *)current_workdir, length)
        ? length : (uint64_t)-EFAULT;
}
/* FS-context-owned cwd ref: no extra ISA/provider-specific unmount checks. */
static uint64_t set_cwd_from_path(const char *path) {
    char resolved[256];
    struct nas_fs_node node;
    int rc=nas_fs_lookup_node(path,true,&node,resolved);
    if(rc)return (uint64_t)(int64_t)rc;
    if(!node.is_dir)return (uint64_t)-ENOTDIR;
    rc=nas_vfs_node_pin(&node);
    if(rc)return (uint64_t)(int64_t)rc;
    struct nas_fs_context *fs=TASK.fs;
    if(fs->cwd_pinned && nas_vfs_node_unpin(&fs->cwd_node)) {
        (void)nas_vfs_node_unpin(&node);
        return (uint64_t)-EIO;
    }
    memcpy(current_workdir,resolved,strlen(resolved)+1);
    fs->cwd_node=node;
    fs->cwd_pinned=true;
    return 0;
}
static uint64_t sys_chdir(uint64_t pathname) {
    char path[256];
    uint64_t rc=nas_fs_resolve_path((uint64_t)-100,pathname,path);
    return (int64_t)rc<0 ? rc : set_cwd_from_path(path);
}
static uint64_t sys_fchdir(uint64_t fd) {
    if (fd >= 64 || TASK.fds->entries[fd].kind == FD_CLOSED)
        return (uint64_t)-EBADF;
    const char *path;
    char proc_base[256];
    if (TASK.fds->entries[fd].kind == FD_PROC_DIR) {
        unsigned slot=TASK.fds->entries[fd].slot;
        struct nas_proc_node node={nas_open_files[slot].proc_kind,
                                   nas_open_files[slot].proc_pid,
                                           nas_open_files[slot].proc_tgid,
                                           nas_open_files[slot].proc_fd};
        if(!nas_proc_dir_path(node,proc_base))return (uint64_t)-ENOENT;
        path=proc_base;
    } else if (TASK.fds->entries[fd].kind == FD_TMP_DIR) {
        const struct nas_tmp_file *dir = tmp_by_slot(TASK.fds->entries[fd].slot);
        if (!dir) return (uint64_t)-ENOENT;
        path=tmp_name(dir);
        if(!path)return (uint64_t)-ENOENT;
    } else {
        const struct nas_open_file *opened=
            &nas_open_files[TASK.fds->entries[fd].slot];
        if(TASK.fds->entries[fd].kind!=FD_DIR)return (uint64_t)-ENOTDIR;
        if(opened->boot_entry)path=opened->boot_entry->path;
        else if(opened->vnode_pinned && opened->vnode.is_dir &&
                opened->origin_path[0])path=opened->origin_path;
        else return (uint64_t)-ENOTDIR;
    }
    return set_cwd_from_path(path);
}


#define NAMESPACE_RESULT(expr) return (struct nas_fs_result){ .handled=true, .value=(expr) }
struct nas_fs_result nas_fs_namespace_syscall(enum nas_syscall_op op,
                                            const uint64_t *args) {
    switch (op) {
    case NAS_SC_getcwd: NAMESPACE_RESULT(sys_getcwd_root(args[1], args[2]));
    case NAS_SC_chdir: NAMESPACE_RESULT(sys_chdir(args[1]));
    case NAS_SC_fchdir: NAMESPACE_RESULT(sys_fchdir(args[1]));
    case NAS_SC_link: NAMESPACE_RESULT(sys_linkat_tmp((uint64_t)-100,args[1],(uint64_t)-100,args[2],0));
    case NAS_SC_linkat: NAMESPACE_RESULT(sys_linkat_tmp(args[1],args[2],args[3],args[4],args[5]));
    case NAS_SC_rename: NAMESPACE_RESULT(sys_renameat_tmp((uint64_t)-100, args[1], (uint64_t)-100, args[2], 0));
    case NAS_SC_renameat: NAMESPACE_RESULT(sys_renameat_tmp(args[1], args[2], args[3], args[4], 0));
    case NAS_SC_renameat2: NAMESPACE_RESULT(sys_renameat_tmp(args[1], args[2], args[3], args[4], args[5]));
    case NAS_SC_symlink: NAMESPACE_RESULT(sys_symlinkat_tmp(args[1], (uint64_t)-100, args[2]));
    case NAS_SC_symlinkat: NAMESPACE_RESULT(sys_symlinkat_tmp(args[1], args[2], args[3]));
    case NAS_SC_mkdir: NAMESPACE_RESULT(sys_mkdirat_tmp((uint64_t)-100, args[1], args[2]));
    case NAS_SC_mkdirat: NAMESPACE_RESULT(sys_mkdirat_tmp(args[1], args[2], args[3]));
    case NAS_SC_unlink: NAMESPACE_RESULT(sys_unlinkat_tmp((uint64_t)-100, args[1], 0));
    case NAS_SC_rmdir: NAMESPACE_RESULT(sys_unlinkat_tmp((uint64_t)-100, args[1], 0x200ULL));
    case NAS_SC_unlinkat: NAMESPACE_RESULT(sys_unlinkat_tmp(args[1], args[2], args[3]));
    default: return (struct nas_fs_result){ .handled=false, .value=0 };
    }
}
#undef NAMESPACE_RESULT
