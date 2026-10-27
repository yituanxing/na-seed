/* Shared CPIO/tmpfs stat, directory and mutable inode Linux ABI. */
#include <stdint.h>
#include <stdbool.h>
#include <limine.h>
#include "../errno.h"
#include "../task.h"
#include "../fd.h"
#include "../bootfs.h"
#include "../tmpfs.h"
#include "../lib/string.h"
#include "../mm/usercopy.h"
#include "../arch/arch.h"
#include "../arch/mmu.h"
#include "../arch/linux_fcntl_abi.h"
#include "io.h"
#include "../arch/linux_stat_abi.h"
#include "../drivers/tty.h"
#include "../drivers/pty.h"
#include "../time/clock.h"
#include "lookup.h"
#include "vfs.h"
#include "procfs.h"
#include "syscall.h"
#include "open_path.h"
#include "metadata_syscall.h"
#define TASK (*nas_current_task)

/* Same inode metadata for both fstat() and path-based stat/lstat; critical
 * for a symlink's S_IFLNK type, target length, and nofollow distinction. */
static void fill_tmp_stat(struct nas_linux_stat *st,
                          const struct nas_tmp_file *file) {
    st->dev = 5; st->ino = 5000 + tmp_number(file);
    st->mode = file->mode;
    st->nlink = file->nlink ? (file->is_dir ? 2 : file->nlink) : 0;
    st->size = (int64_t)file->size;
    st->blocks = (int64_t)((file->size + 511) / 512);
    st->atime_sec = (int64_t)(file->atime_ns / NAS_NSEC_PER_SEC);
    st->atime_nsec = file->atime_ns % NAS_NSEC_PER_SEC;
    st->mtime_sec = (int64_t)(file->mtime_ns / NAS_NSEC_PER_SEC);
    st->mtime_nsec = file->mtime_ns % NAS_NSEC_PER_SEC;
    st->ctime_sec = (int64_t)(file->ctime_ns / NAS_NSEC_PER_SEC);
    st->ctime_nsec = file->ctime_ns % NAS_NSEC_PER_SEC;
}
/* Generic inode stat comes from the filesystem lookup result, never a
 * fixed ext4-only number or an exported kernel pointer. */
static uint64_t write_mounted_stat(const struct nas_fs_node *node,
                                   uint64_t destination) {
    if(!node || !node->filesystem || !node->ino)return (uint64_t)-EINVAL;
    struct nas_linux_stat st={0};
    st.dev=nas_vfs_node_device(node);
    if(!st.dev)return (uint64_t)-ENODEV;
    st.ino=node->ino;st.mode=node->mode;
    st.nlink=node->nlink ? node->nlink : (node->is_dir ? 2 : 1);
    st.uid=node->uid;st.gid=node->gid;
    uint64_t live_size=nas_vfs_node_size(node);
    st.size=(int64_t)live_size;
    st.blocks=(int64_t)((live_size+511)/512);
    st.blksize=PAGE;
    st.atime_sec=(int64_t)node->atime_sec;
    st.mtime_sec=(int64_t)node->mtime_sec;
    st.ctime_sec=(int64_t)node->ctime_sec;
    return nas_user_write_bytes(destination,(const uint8_t *)&st,sizeof(st))
        ? 0:(uint64_t)-EFAULT;
}
/* Linux x86-64 struct stat layout, 144 bytes. File size is actual module.
 * Device entries have character-device type; mounted files are regular.
 */
static uint64_t sys_fstat_boot(uint64_t fd, uint64_t address) {
    if (fd >= 64 || !TASK.fds->entries[fd].kind) return (uint64_t)-EBADF;
    struct nas_linux_stat st;
    volatile uint8_t *bytes = (volatile uint8_t *)&st;
    for (unsigned i = 0; i < sizeof(st); i++) bytes[i] = 0;
    st.nlink = 1;
    st.blksize = PAGE;
    if (TASK.fds->entries[fd].kind == FD_DIR || TASK.fds->entries[fd].kind == FD_ARCHIVE) {
        const struct bootfs_entry *entry = nas_open_files[TASK.fds->entries[fd].slot].boot_entry;
        if(entry) {
            st.dev=3;st.ino=entry->ino;st.mode=entry->mode;
            st.nlink=bootfs_is_dir(entry)?2:entry->nlink;
            st.size=(int64_t)entry->size;
            st.blocks=(int64_t)((entry->size+511)/512);
            st.atime_sec=st.mtime_sec=st.ctime_sec=(int64_t)entry->mtime_sec;
        } else {
            /* A generic mounted inode is not a bootfs archive record. */
            const struct nas_open_file *open=&nas_open_files[TASK.fds->entries[fd].slot];
            if(!open->vnode_pinned)return (uint64_t)-EBADF;
            return write_mounted_stat(&open->vnode,address);
        }
    } else if (TASK.fds->entries[fd].kind == FD_TMP || TASK.fds->entries[fd].kind == FD_TMP_DIR) {
        const struct nas_tmp_file *file = tmp_by_slot(TASK.fds->entries[fd].slot);
        if (!file) return (uint64_t)-EBADF;
        fill_tmp_stat(&st,file);
    } else if (TASK.fds->entries[fd].kind == FD_PROC_FILE ||
               TASK.fds->entries[fd].kind == FD_PROC_DIR) {
        unsigned slot=TASK.fds->entries[fd].slot;
        struct nas_proc_node node={nas_open_files[slot].proc_kind,
                                   nas_open_files[slot].proc_pid,
                                   nas_open_files[slot].proc_tgid,
                                   nas_open_files[slot].proc_fd};
        st.dev=7;st.ino=nas_proc_ino(node);
        st.mode=nas_proc_mode(node);
        st.nlink=nas_proc_is_dir(node)?2:1;
    } else if (TASK.fds->entries[fd].kind == FD_PTY_MASTER ||
               TASK.fds->entries[fd].kind == FD_PTY_SLAVE) {
        st.dev=6;st.ino=2000+nas_pty_number(TASK.fds->entries[fd].slot);
        st.mode=0020620; /* real PTY character device */
    } else if (TASK.fds->entries[fd].kind == FD_PIPE_R || TASK.fds->entries[fd].kind == FD_PIPE_W ||
               TASK.fds->entries[fd].kind == FD_SOCKET) {
        st.dev = 4; st.ino = (uint64_t)TASK.fds->entries[fd].slot + 1000;
        st.mode = TASK.fds->entries[fd].kind == FD_SOCKET ? 0140777 : 0010600;
        /* Linux presents socketpair as S_IFSOCK, pipe as S_IFIFO. */
    } else if (TASK.fds->entries[fd].kind == FD_BOOTFS) {
        const struct limine_file *file = nas_open_files[TASK.fds->entries[fd].slot].boot_file;
        if (!file) return (uint64_t)-EBADF;
        st.dev = 1; st.ino = fd + 100;
        st.mode = 0100555;
        st.size = (int64_t)file->size;
        st.blocks = (int64_t)((file->size + 511) / 512);
    } else {
        st.dev = 2; st.ino = fd + 10;
        st.mode = 0020666; /* real character device backed by UART/null/zero */
    }
    if (!nas_user_write_bytes(address, (const uint8_t *)&st, sizeof(st)))
        return (uint64_t)-EFAULT;
    return 0;
}
/* In a volatile tmpfs there is no asynchronous durable writeback.
 * fsync/fdatasync return success only for actual open RAM inodes. */
static uint64_t sys_sync_fd(uint64_t fd) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)return (uint64_t)-EBADF;
    if(nas_open_files[TASK.fds->entries[fd].slot].status_flags&NAS_LINUX_O_PATH)
        return (uint64_t)-EBADF;
    const struct nas_open_file *opened=&nas_open_files[TASK.fds->entries[fd].slot];
    if(!opened->vnode_pinned)return (uint64_t)-EINVAL;
    return (uint64_t)(int64_t)nas_vfs_sync(&opened->vnode);
}
static uint64_t sys_resize_fd(uint64_t fd,uint64_t length) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)return (uint64_t)-EBADF;
    if(nas_open_files[TASK.fds->entries[fd].slot].status_flags&NAS_LINUX_O_PATH)
        return (uint64_t)-EBADF;
    if(TASK.fds->entries[fd].kind==FD_BOOTFS)return (uint64_t)-EROFS;
    if(TASK.fds->entries[fd].kind!=FD_TMP &&
       TASK.fds->entries[fd].kind!=FD_ARCHIVE)return (uint64_t)-EINVAL;
    unsigned slot=TASK.fds->entries[fd].slot;
    struct nas_open_file *opened=&nas_open_files[slot];
    if((opened->status_flags&3ULL)==0)return (uint64_t)-EINVAL;
    if(!opened->vnode_pinned)return (uint64_t)-EBADF;
    int rc=nas_vfs_resize(&opened->vnode,length);
    if(!rc)opened->vnode.size=length;
    return (uint64_t)(int64_t)rc;
}
static uint64_t sys_resize_path(uint64_t pathname,uint64_t length) {
    char path[256];
    uint64_t ret=nas_fs_resolve_path((uint64_t)-100,pathname,path);
    if((int64_t)ret<0)return ret;
    char resolved[256];
    struct nas_fs_node node;
    int rc=nas_fs_lookup_node(path,true,&node,resolved);
    return rc ? (uint64_t)(int64_t)rc :
           (uint64_t)(int64_t)nas_vfs_resize(&node,length);
}
static uint64_t sys_lseek_boot(uint64_t fd, uint64_t off, uint64_t whence) {
    if (fd >= 64 || TASK.fds->entries[fd].kind == FD_CLOSED)
        return (uint64_t)-EBADF;
    if(nas_open_files[TASK.fds->entries[fd].slot].status_flags&NAS_LINUX_O_PATH)
        return (uint64_t)-EBADF;
    uint64_t size = 0;
    if (TASK.fds->entries[fd].kind == FD_BOOTFS && nas_open_files[TASK.fds->entries[fd].slot].boot_file)
        size = nas_open_files[TASK.fds->entries[fd].slot].boot_file->size;
    else if ((TASK.fds->entries[fd].kind == FD_ARCHIVE ||
              TASK.fds->entries[fd].kind == FD_DIR) &&
             nas_open_files[TASK.fds->entries[fd].slot].vnode_pinned)
        size = nas_vfs_node_size(&nas_open_files[TASK.fds->entries[fd].slot].vnode);
    else if (TASK.fds->entries[fd].kind == FD_TMP || TASK.fds->entries[fd].kind == FD_TMP_DIR) {
        const struct nas_tmp_file *file = tmp_by_slot(TASK.fds->entries[fd].slot);
        if (!file) return (uint64_t)-EBADF;
        size = file->size;
    }
    else if (TASK.fds->entries[fd].kind == FD_PROC_DIR ||
             TASK.fds->entries[fd].kind == FD_PROC_FILE)
        size=0; /* dynamic proc files report zero st_size, as Linux does */
    else if (TASK.fds->entries[fd].kind != FD_DIR || !nas_open_files[TASK.fds->entries[fd].slot].boot_entry)
        return (uint64_t)-ESPIPE;
    uint64_t base = 0;
    if (whence == 1) base = nas_open_files[TASK.fds->entries[fd].slot].position;
    else if (whence == 2 && TASK.fds->entries[fd].kind != FD_DIR &&
             TASK.fds->entries[fd].kind != FD_TMP_DIR &&
             TASK.fds->entries[fd].kind != FD_PROC_DIR) base = size;
    else if (whence != 0) return (uint64_t)-EINVAL;
    int64_t signed_off = (int64_t)off;
    if (signed_off < 0) {
        uint64_t magnitude = 0 - off;
        if (magnitude > base) return (uint64_t)-EINVAL;
        base -= magnitude;
    } else {
        if (base > UINT64_MAX - off) return (uint64_t)-EINVAL;
        base += off;
    }
    nas_open_files[TASK.fds->entries[fd].slot].position = base;
    return base;
}
/* Linux access/faccessat/faccessat2: existence and permissions are
 * checked against the actual CPIO /tmp/device namespace, not a fabricated
 * "always exists" or "always readable" response.  Our current credentials
 * are root: read/write bypass DAC bits, but execute needs at least one X bit.
 * An immutable CPIO file cannot be written even by root (EROFS).
 */
static uint64_t sys_access_path(uint64_t dirfd,uint64_t pathname,
                                uint64_t mode,uint64_t flags) {
    const uint64_t AT_EACCESS=0x200ULL;
    const uint64_t AT_SYMLINK_NOFOLLOW=0x100ULL;
    if((mode & ~7ULL) || (flags & ~(AT_EACCESS|AT_SYMLINK_NOFOLLOW)))
        return (uint64_t)-EINVAL;
    char path[256], actual[256];
    uint64_t rc=nas_fs_resolve_path(dirfd,pathname,path);
    if((int64_t)rc<0) return rc;
    uint32_t bits=0;
    bool writable=false;
    bool is_dir=false;
    struct nas_fs_node node;
    int err=nas_fs_lookup_node(path,!(flags&AT_SYMLINK_NOFOLLOW),
                               &node,actual);
    if(!err) {
        bits=node.mode;
        is_dir=node.is_dir;
        writable=nas_vfs_can_write(&node);
    } else if(err!=-ENOENT)return (uint64_t)(int64_t)err;
    if(!bits && (!strcmp(path,"/dev/ptmx") ||
                 (nas_pty_path_number(path)>=0 &&
                  nas_pty_present((unsigned)nas_pty_path_number(path))) ||
                 !strcmp(path,"/dev/null") || !strcmp(path,"/dev/zero") ||
                 !strcmp(path,"/dev/console") || !strcmp(path,"/dev/ttyS0") ||
                 !strcmp(path,"/dev/tty"))) {
        if(!strcmp(path,"/dev/tty") && !nas_tty_controlling() &&
           !nas_pty_controlling())
            return (uint64_t)-ENXIO;
        bits=0020666U; writable=true;
    }
    if(!bits)return (uint64_t)-ENOENT;
    if((mode & 1ULL) && !(bits & 0111U))return (uint64_t)-EACCES;
    if((mode & 2ULL) && !writable)return (uint64_t)-EROFS;
    if((mode & 2ULL) && !is_dir && (bits&0170000U)==0040000U)
        return (uint64_t)-EACCES;
    return 0;
}
/* chmod operates on tmpfs inode identity (not FD pathname copies).
 * Linux ABI/path and dirfd decoding stay in this syscall boundary. */
static uint64_t sys_fchmod_fd(uint64_t fd,uint64_t mode) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)
        return (uint64_t)-EBADF;
    if(TASK.fds->entries[fd].kind!=FD_TMP && TASK.fds->entries[fd].kind!=FD_TMP_DIR)
        return (uint64_t)-EROFS;
    return (uint64_t)(int64_t)tmp_chmod((struct nas_tmp_file*)tmp_by_slot(TASK.fds->entries[fd].slot),mode);
}
static uint64_t sys_chmod_path(uint64_t dirfd,uint64_t filename,
                               uint64_t mode) {
    char path[256], actual[256];
    uint64_t ret=nas_fs_resolve_path(dirfd,filename,path);
    if((int64_t)ret<0)return ret;
    struct nas_fs_node node;
    int err=nas_fs_lookup_node(path,true,&node,actual);
    if(err)return (uint64_t)(int64_t)err;
    return (uint64_t)(int64_t)nas_vfs_chmod(&node,mode);
}
/* Linux utimensat: native 64-bit timespec ABI common to all three ISAs.
 * The VFS resolves one real inode; CPIO never becomes writable. None of
 * this logic belongs in an architecture or GNU Make special-case handler.
 * UTIME_NOW and UTIME_OMIT have their real Linux values. */
static uint64_t sys_utimensat_file(uint64_t dirfd, uint64_t pathname,
                                   uint64_t timesptr, uint64_t flags) {
    const uint64_t AT_SYMLINK_NOFOLLOW=0x100ULL;
    const uint64_t AT_EMPTY_PATH=0x1000ULL;
    const uint64_t UTIME_NOW=0x3fffffffULL;
    const uint64_t UTIME_OMIT=0x3ffffffeULL;
    if (flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH))
        return (uint64_t)-EINVAL;
    struct nas_tmp_file *file=0;
    struct nas_fs_node vnode={0};
    if (!pathname) {
        if (dirfd >= NAS_FD_LIMIT || TASK.fds->entries[dirfd].kind == FD_CLOSED)
            return (uint64_t)-EBADF;
        if (TASK.fds->entries[dirfd].kind != FD_TMP &&
            TASK.fds->entries[dirfd].kind != FD_TMP_DIR)
            return (uint64_t)-EROFS;
        file=tmp_by_slot(TASK.fds->entries[dirfd].slot);
        if (!file) return (uint64_t)-ENOENT;
    } else {
        char path[256], actual[256];
        uint64_t ret=nas_fs_resolve_path(dirfd,pathname,path);
        if ((int64_t)ret<0) return ret;
        int err=nas_fs_lookup_node(path,!(flags&AT_SYMLINK_NOFOLLOW),
                                   &vnode,actual);
        if (err) return (uint64_t)(int64_t)err;
        if (!nas_vfs_can_write(&vnode)) return (uint64_t)-EROFS;
    }
    uint64_t now=0;
    if (!nas_time_realtime_ns(&now)) now=nas_arch_monotonic_ns();
    uint64_t ns[2]={now,now};
    bool set[2]={true,true};
    if (timesptr) {
        int64_t raw[4];
        if (!nas_user_read_bytes(timesptr,(uint8_t *)raw,sizeof(raw)))
            return (uint64_t)-EFAULT;
        for (unsigned i=0;i<2;i++) {
            uint64_t nsec=(uint64_t)raw[i*2+1];
            if (nsec==UTIME_OMIT) { set[i]=false; continue; }
            if (nsec==UTIME_NOW) continue;
            if (raw[i*2]<0 || raw[i*2]>INT64_MAX/(int64_t)NAS_NSEC_PER_SEC ||
                raw[i*2+1]<0 || nsec>=NAS_NSEC_PER_SEC)
                return (uint64_t)-EINVAL;
            ns[i]=(uint64_t)raw[i*2]*NAS_NSEC_PER_SEC+nsec;
        }
    }
    return (uint64_t)(int64_t)(pathname
        ? nas_vfs_utimens(&vnode,ns[0],ns[1],set[0],set[1])
        : tmp_utimens(file,ns[0],ns[1],set[0],set[1]));
}
static uint64_t write_boot_stat(const struct bootfs_entry *entry, uint64_t dst) {
    struct nas_linux_stat st;
    uint8_t *p = (uint8_t *)&st;
    for (unsigned i = 0; i < sizeof(st); i++) p[i] = 0;
    st.dev = 3;
    st.ino = entry->ino;
    st.nlink = bootfs_is_dir(entry) ? 2 : entry->nlink;
    st.mode = entry->mode;
    st.blksize = PAGE;
    st.size = (int64_t)entry->size;
    st.blocks = (int64_t)((entry->size + 511) / 512);
    st.atime_sec = st.mtime_sec = st.ctime_sec = (int64_t)entry->mtime_sec;
    return nas_user_write_bytes(dst, (const uint8_t *)&st, sizeof(st))
        ? 0 : (uint64_t)-EFAULT;
}
static uint64_t write_tmp_stat(const struct nas_tmp_file *file,uint64_t dst) {
    struct nas_linux_stat st={0};
    st.blksize=PAGE;
    fill_tmp_stat(&st,file);
    return nas_user_write_bytes(dst,(const uint8_t *)&st,sizeof(st)) ?
           0 : (uint64_t)-EFAULT;
}
static uint64_t write_proc_stat(struct nas_proc_node node,uint64_t dst) {
    struct nas_linux_stat st={0};
    st.dev=7;
    st.ino=nas_proc_ino(node);
    st.mode=nas_proc_mode(node);
    st.nlink=nas_proc_is_dir(node)?2:1;
    st.blksize=PAGE;
    return nas_user_write_bytes(dst,(const uint8_t *)&st,sizeof(st))
        ? 0:(uint64_t)-EFAULT;
}
static uint64_t sys_fstatat_boot(uint64_t dirfd, uint64_t pathname,
                                 uint64_t statbuf, uint64_t flags) {
    const uint64_t AT_SYMLINK_NOFOLLOW = 0x100ULL;
    const uint64_t AT_EMPTY_PATH = 0x1000ULL;
    if (flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH))
        return (uint64_t)-EINVAL;
    uint8_t first;
    if (!nas_user_read_bytes(pathname, &first, 1)) return (uint64_t)-EFAULT;
    if (first == 0 && (flags & AT_EMPTY_PATH))
        return sys_fstat_boot(dirfd, statbuf);
    char path[256];
    uint64_t result = nas_fs_resolve_path(dirfd, pathname, path);
    if ((int64_t)result < 0) return result;
    struct nas_proc_node proc;
    if(nas_proc_lookup(path,&proc))return write_proc_stat(proc,statbuf);
    if(!strcmp(path,"/dev/ptmx") ||
       (nas_pty_path_number(path)>=0 &&
        nas_pty_present((unsigned)nas_pty_path_number(path)))) {
        struct nas_linux_stat device={0};
        device.dev=6;device.ino=2000;
        device.mode=0020620;device.nlink=1;device.blksize=PAGE;
        return nas_user_write_bytes(statbuf,(const uint8_t *)&device,sizeof(device))
            ? 0:(uint64_t)-EFAULT;
    }
    if (bootfs_mounted()) {
        char actual[256];
        int err = nas_fs_resolve_existing(path, !(flags & AT_SYMLINK_NOFOLLOW), actual);
        if (!err) {
            struct nas_fs_node node={0};
            int rc=nas_vfs_lookup(actual,&node);
            if(rc)return (uint64_t)(int64_t)rc;
            const struct nas_tmp_file *tmp=tmp_find(actual);
            if(tmp && node.inode==(const void *)tmp)
                return write_tmp_stat(tmp,statbuf);
            const struct bootfs_entry *entry=bootfs_find(actual);
            if(entry && node.inode==(const void *)entry)
                return write_boot_stat(entry,statbuf);
            return write_mounted_stat(&node,statbuf);
        }
        if (err != -ENOENT) return (uint64_t)(int64_t)err;
    }
    /* Legacy modules and character devices retain their existing fstat. */
    result = nas_fs_open_path(path, 0, 0);
    if ((int64_t)result < 0) return result;
    uint64_t ret = sys_fstat_boot(result, statbuf);
    nas_fs_close_fd(result);
    return ret;
}

/* Linux x86-64 linux_dirent64: ino64, off64, reclen16, dtype8, name[].
 * The offset cookie indexes '.', '..', then direct children of a directory.
 */
/* Linux readlink/readlinkat return the link bytes without a terminating NUL,
 * and deliberately do not follow the last component. */
static uint64_t sys_readlinkat_boot(uint64_t dirfd, uint64_t pathname,
                                     uint64_t dst, uint64_t capacity) {
    if (!capacity) return (uint64_t)-EINVAL;
    char path[256], actual[256];
    uint64_t r = nas_fs_resolve_path(dirfd, pathname, path);
    if ((int64_t)r < 0) return r;
    struct nas_fs_node node={0};
    int error=nas_fs_lookup_node(path,false,&node,actual);
    if(error)return (uint64_t)(int64_t)error;
    if((node.mode&0170000U)!=0120000U)return (uint64_t)-EINVAL;
    char target[256];
    int got=nas_vfs_readlink(&node,actual,target);
    if(got<0)return (uint64_t)(int64_t)got;
    uint64_t size=(uint64_t)got<capacity?(uint64_t)got:capacity;
    return nas_user_write_bytes(dst,(const uint8_t *)target,size)
        ? size:(uint64_t)-EFAULT;
}

/* Translate any mounted provider readdir callback to Linux dirent64,
 * without exposing host/ext4 structures or accessing user memory in drivers.
 * The logical cookie resides in the shared open File, not in a task FD. */
static uint64_t vfs_getdents64(uint64_t fd,uint64_t user,uint64_t capacity) {
    struct nas_open_file *opened=&nas_open_files[TASK.fds->entries[fd].slot];
    if(opened->status_flags&NAS_LINUX_O_PATH)return (uint64_t)-EBADF;
    if(!opened->vnode.is_dir)return (uint64_t)-ENOTDIR;
    if(capacity<24)return (uint64_t)-EINVAL;
    if(user>=USER_LIMIT || capacity>USER_LIMIT-user)return (uint64_t)-EFAULT;
    uint64_t cursor=opened->position,written=0;
    struct nas_vfs_dirent batch[8];
    while(capacity-written>=24) {
        int count=nas_vfs_read_dir(&opened->vnode,opened->origin_path,
                                   cursor,batch,8);
        if(count<0)return written?written:(uint64_t)(int64_t)count;
        if(!count)break;
        if(count>8)return written?written:(uint64_t)-EIO;
        for(int i=0;i<count;i++) {
            const struct nas_vfs_dirent *e=&batch[i];
            unsigned length=e->name_len;
            if(!e->ino || !length ||
               e->name[length]!=0)return written?written:(uint64_t)-EIO;
            unsigned record_bytes=(19+length+1+7)&~7U;
            if(record_bytes>280)return written?written:(uint64_t)-EIO;
            if(record_bytes>capacity-written)
                return written?written:(uint64_t)-EINVAL;
            uint8_t raw[280]={0};
            for(unsigned b=0;b<8;b++) {
                raw[b]=(uint8_t)(e->ino>>(b*8));
                raw[8+b]=(uint8_t)((cursor+1)>>(b*8));
            }
            raw[16]=(uint8_t)record_bytes;
            raw[17]=(uint8_t)(record_bytes>>8);
            raw[18]=e->dtype;
            memcpy(raw+19,e->name,length);
            if(!nas_user_write_bytes(user+written,raw,record_bytes))
                return written?written:(uint64_t)-EFAULT;
            written+=record_bytes;
            opened->position=++cursor;
        }
    }
    return written;
}
static uint64_t sys_getdents64(uint64_t fd,uint64_t dst,uint64_t capacity) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)
        return (uint64_t)-EBADF;
    const struct nas_open_file *file=
        &nas_open_files[TASK.fds->entries[fd].slot];
    if(!file->vnode_pinned || !file->vnode.is_dir)
        return (uint64_t)-ENOTDIR;
    return vfs_getdents64(fd,dst,capacity);
}

#define METADATA_RESULT(expr) return (struct nas_fs_result){ .handled=true, .value=(expr) }
struct nas_fs_result nas_fs_metadata_syscall(enum nas_syscall_op op,
                                            const uint64_t *args) {
    switch (op) {
    case NAS_SC_chmod: METADATA_RESULT(sys_chmod_path((uint64_t)-100,args[1],args[2]));
    case NAS_SC_fchmod: METADATA_RESULT(sys_fchmod_fd(args[1],args[2]));
    case NAS_SC_fchmodat: METADATA_RESULT(sys_chmod_path(args[1],args[2],args[3]));
    case NAS_SC_utimensat: METADATA_RESULT(sys_utimensat_file(args[1],args[2],args[3],args[4]));
    case NAS_SC_access: METADATA_RESULT(sys_access_path((uint64_t)-100,args[1],args[2],0));
    case NAS_SC_faccessat: METADATA_RESULT(sys_access_path(args[1],args[2],args[3],0));
    case NAS_SC_faccessat2: METADATA_RESULT(sys_access_path(args[1],args[2],args[3],args[4]));
    case NAS_SC_fstat: METADATA_RESULT(sys_fstat_boot(args[1], args[2]));
    case NAS_SC_newfstatat: METADATA_RESULT(sys_fstatat_boot(args[1], args[2], args[3], args[4]));
    case NAS_SC_stat: METADATA_RESULT(sys_fstatat_boot((uint64_t)-100, args[1], args[2], 0));
    case NAS_SC_lstat: METADATA_RESULT(sys_fstatat_boot((uint64_t)-100, args[1], args[2], 0x100ULL));
    case NAS_SC_lseek: METADATA_RESULT(sys_lseek_boot(args[1], args[2], args[3]));
    case NAS_SC_fsync: case NAS_SC_fdatasync: METADATA_RESULT(sys_sync_fd(args[1]));
    case NAS_SC_ftruncate: METADATA_RESULT(sys_resize_fd(args[1],args[2]));
    case NAS_SC_truncate: METADATA_RESULT(sys_resize_path(args[1],args[2]));
    case NAS_SC_getdents64: METADATA_RESULT(sys_getdents64(args[1], args[2], args[3]));
    case NAS_SC_readlink: METADATA_RESULT(sys_readlinkat_boot((uint64_t)-100, args[1], args[2], args[3]));
    case NAS_SC_readlinkat: METADATA_RESULT(sys_readlinkat_boot(args[1], args[2], args[3], args[4]));
    default: return (struct nas_fs_result){ .handled=false, .value=0 };
    }
}
#undef METADATA_RESULT
