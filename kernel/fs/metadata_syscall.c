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
        if (!entry) return (uint64_t)-EBADF;
        st.dev = 3; st.ino = entry->ino;
        st.mode = entry->mode;
        st.nlink = bootfs_is_dir(entry) ? 2 : entry->nlink;
        st.size = (int64_t)entry->size;
        st.blocks = (int64_t)((entry->size + 511) / 512);
        st.atime_sec = st.mtime_sec = st.ctime_sec = (int64_t)entry->mtime_sec;
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
    if(TASK.fds->entries[fd].kind!=FD_TMP && TASK.fds->entries[fd].kind!=FD_TMP_DIR)
        return (uint64_t)-EINVAL;
    return tmp_by_slot(TASK.fds->entries[fd].slot) ? 0 : (uint64_t)-EBADF;
}
static uint64_t sys_resize_fd(uint64_t fd,uint64_t length) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)return (uint64_t)-EBADF;
    if(nas_open_files[TASK.fds->entries[fd].slot].status_flags&NAS_LINUX_O_PATH)
        return (uint64_t)-EBADF;
    if(TASK.fds->entries[fd].kind==FD_ARCHIVE || TASK.fds->entries[fd].kind==FD_BOOTFS)
        return (uint64_t)-EROFS;
    if(TASK.fds->entries[fd].kind!=FD_TMP)return (uint64_t)-EINVAL;
    if((nas_open_files[TASK.fds->entries[fd].slot].status_flags&3ULL)==0)
        return (uint64_t)-EINVAL;
    return (uint64_t)(int64_t)tmp_resize(tmp_by_slot(TASK.fds->entries[fd].slot),length);
}
static uint64_t sys_resize_path(uint64_t pathname,uint64_t length) {
    char path[256];
    uint64_t ret=nas_fs_resolve_path((uint64_t)-100,pathname,path);
    if((int64_t)ret<0)return ret;
    struct nas_tmp_file *file=tmp_find(path);
    if(file)return (uint64_t)(int64_t)tmp_resize(file,length);
    return bootfs_find(path) ? (uint64_t)-EROFS : (uint64_t)-ENOENT;
}
static uint64_t sys_lseek_boot(uint64_t fd, uint64_t off, uint64_t whence) {
    if (fd >= 64 || TASK.fds->entries[fd].kind == FD_CLOSED)
        return (uint64_t)-EBADF;
    if(nas_open_files[TASK.fds->entries[fd].slot].status_flags&NAS_LINUX_O_PATH)
        return (uint64_t)-EBADF;
    uint64_t size = 0;
    if (TASK.fds->entries[fd].kind == FD_BOOTFS && nas_open_files[TASK.fds->entries[fd].slot].boot_file)
        size = nas_open_files[TASK.fds->entries[fd].slot].boot_file->size;
    else if (TASK.fds->entries[fd].kind == FD_ARCHIVE && nas_open_files[TASK.fds->entries[fd].slot].boot_entry)
        size = nas_open_files[TASK.fds->entries[fd].slot].boot_entry->size;
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
        writable=node.backend==NAS_FS_TMPFS;
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
    if(node.backend!=NAS_FS_TMPFS)return (uint64_t)-EROFS;
    return (uint64_t)(int64_t)tmp_chmod(tmp_find(actual),mode);
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
    if (!pathname) {
        if (dirfd >= NAS_FD_LIMIT || TASK.fds->entries[dirfd].kind == FD_CLOSED)
            return (uint64_t)-EBADF;
        if (TASK.fds->entries[dirfd].kind != FD_TMP && TASK.fds->entries[dirfd].kind != FD_TMP_DIR)
            return (uint64_t)-EROFS;
        file=tmp_by_slot(TASK.fds->entries[dirfd].slot);
    } else {
        char path[256], actual[256];
        uint64_t ret=nas_fs_resolve_path(dirfd,pathname,path);
        if ((int64_t)ret<0) return ret;
        struct nas_fs_node node;
        int err=nas_fs_lookup_node(path,!(flags&AT_SYMLINK_NOFOLLOW),
                                   &node,actual);
        if (err) return (uint64_t)(int64_t)err;
        if (node.backend!=NAS_FS_TMPFS) return (uint64_t)-EROFS;
        file=(struct nas_tmp_file *)(uintptr_t)node.tmp;
    }
    if (!file) return (uint64_t)-ENOENT;
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
    return (uint64_t)(int64_t)tmp_utimens(file,ns[0],ns[1],set[0],set[1]);
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
            const struct nas_tmp_file *tmp=tmp_find(actual);
            if (tmp) return write_tmp_stat(tmp,statbuf);
            return write_boot_stat(bootfs_find(actual),statbuf);
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
    struct nas_proc_node proc_node={0};
    if(nas_proc_lookup(path,&proc_node) &&
       proc_node.kind==NAS_PROC_FD_LINK) {
        char target[256];
        int64_t got=nas_proc_readlink(proc_node,target,sizeof(target));
        if(got<0)return (uint64_t)got;
        uint64_t n=(uint64_t)got<capacity?(uint64_t)got:capacity;
        return nas_user_write_bytes(dst,(const uint8_t *)target,n) ?
               n:(uint64_t)-EFAULT;
    }
    int err = nas_fs_resolve_existing(path, false, actual);
    if (err) return (uint64_t)(int64_t)err;
    const struct nas_tmp_file *tmp=tmp_find(actual);
    if (tmp) {
        if (!tmp_is_link(tmp)) return (uint64_t)-EINVAL;
        uint8_t content[256];
        int64_t got=tmp_read_at(tmp,0,content,
                     tmp->size<capacity?tmp->size:capacity);
        if (got<0) return (uint64_t)got;
        return nas_user_write_bytes(dst,content,(uint64_t)got) ?
               (uint64_t)got : (uint64_t)-EFAULT;
    }
    const struct bootfs_entry *entry = bootfs_find(actual);
    if (!bootfs_is_link(entry)) return (uint64_t)-EINVAL;
    uint64_t len = entry->size < capacity ? entry->size : capacity;
    return nas_user_write_bytes(dst, entry->data, len) ? len : (uint64_t)-EFAULT;
}

static uint64_t proc_getdents64(uint64_t fd,uint64_t dst,uint64_t capacity) {
    if(capacity<24)return (uint64_t)-EINVAL;
    if(dst>=USER_LIMIT || capacity>USER_LIMIT-dst)return (uint64_t)-EFAULT;
    unsigned slot=TASK.fds->entries[fd].slot;
    if(nas_open_files[slot].status_flags&NAS_LINUX_O_PATH)
        return (uint64_t)-EBADF;
    struct nas_proc_node dir={nas_open_files[slot].proc_kind,
                              nas_open_files[slot].proc_pid,
                                   nas_open_files[slot].proc_tgid,
                                   nas_open_files[slot].proc_fd};
    uint64_t cursor=nas_open_files[slot].position,written=0;
    while(written+24<=capacity) {
        char name[32];
        struct nas_proc_node child={0};
        unsigned dtype=4;
        uint64_t ino=nas_proc_ino(dir);
        if(cursor==0){name[0]='.';name[1]=0;}
        else if(cursor==1){name[0]='.';name[1]='.';name[2]=0;}
        else {
            if(cursor-2>UINT32_MAX ||
               !nas_proc_child(dir,(unsigned)(cursor-2),name,&child))break;
            dtype=nas_proc_is_dir(child)?4:
                  child.kind==NAS_PROC_FD_LINK?10:8;
            ino=nas_proc_ino(child);
        }
        unsigned len=0;while(name[len])len++;
        unsigned reclen=(19+len+1+7)&~7U;
        if(reclen>capacity-written) {
            if(!written)return (uint64_t)-EINVAL;
            break;
        }
        uint8_t record[64]={0};
        for(unsigned i=0;i<8;i++) {
            record[i]=(uint8_t)(ino>>(i*8));
            record[8+i]=(uint8_t)((cursor+1)>>(i*8));
        }
        record[16]=(uint8_t)reclen;
        record[17]=(uint8_t)(reclen>>8);
        record[18]=(uint8_t)dtype;
        for(unsigned i=0;i<len;i++)record[19+i]=(uint8_t)name[i];
        if(!nas_user_write_bytes(dst+written,record,reclen))
            return written?written:(uint64_t)-EFAULT;
        written+=reclen;cursor++;
        nas_open_files[slot].position=cursor;
    }
    return written;
}
static uint64_t sys_getdents64(uint64_t fd, uint64_t dst, uint64_t capacity) {
    if (fd >= 64 || TASK.fds->entries[fd].kind == FD_CLOSED) return (uint64_t)-EBADF;
    if(TASK.fds->entries[fd].kind==FD_PROC_DIR)
        return proc_getdents64(fd,dst,capacity);
    if (TASK.fds->entries[fd].kind != FD_TMP_DIR &&
        (TASK.fds->entries[fd].kind != FD_DIR || !nas_open_files[TASK.fds->entries[fd].slot].boot_entry))
        return (uint64_t)-ENOTDIR;
    if (capacity < 24) return (uint64_t)-EINVAL;
    if (dst >= USER_LIMIT || capacity > USER_LIMIT - dst)
        return (uint64_t)-EFAULT;
    const struct bootfs_entry *dir = nas_open_files[TASK.fds->entries[fd].slot].boot_entry;
    const struct nas_tmp_file *tmpdir =
        TASK.fds->entries[fd].kind == FD_TMP_DIR ? tmp_by_slot(TASK.fds->entries[fd].slot) : 0;
    const char *dirpath = tmpdir ? tmp_name(tmpdir) : dir->path;
    if(!dirpath)return (uint64_t)-ENOENT;
    uint64_t dirino = tmpdir ? 5000 + tmp_number(tmpdir) : dir->ino;
    uint64_t cursor = nas_open_files[TASK.fds->entries[fd].slot].position;
    uint64_t written = 0;
    while (written + 24 <= capacity) {
        const struct bootfs_entry *item;
        const char *name;
        uint64_t ino;
        unsigned dtype;
        if (cursor == 0) {
            name = "."; ino = dirino; dtype = 4;
        } else if (cursor == 1) {
            name = ".."; dtype = 4;
            char parent[256];
            unsigned len = 0;
            while (dirpath[len]) { parent[len] = dirpath[len]; len++; }
            while (len > 1 && parent[len - 1] != '/') len--;
            if (len > 1) len--;
            parent[len] = 0;
            item = bootfs_find(parent);
            struct nas_tmp_file *tparent = tmp_find(parent);
            ino = tparent ? 5000 + tmp_number(tparent) :
                  item ? item->ino : dirino;
        } else if ((strcmp(dirpath, "/tmp") == 0) || tmpdir) {
            /* tmpfs uses lazy metadata slabs: end of directory is the
             * first missing child, never a global eight-inode bound. */
            if (cursor - 2 > UINT32_MAX) break;
            const struct nas_tmp_dentry *d =
                tmp_child_entry(dirpath, (unsigned)(cursor - 2));
            if (!d) break;
            const struct nas_tmp_file *file=d->inode;
            name = d->path;
            for (const char *p = d->path; *p; p++)
                if (*p == '/') name = p + 1;
            ino = 5000 + tmp_number(file);
            dtype = file->is_dir ? 4 : tmp_is_link(file) ? 10 :
                    (file->mode&0170000U)==0140000U ? 12 : 8;
        } else {
            if (cursor - 2 > UINT32_MAX) break;
            item = bootfs_child(dir, (unsigned)(cursor - 2));
            if (!item) break;
            name = item->path;
            for (const char *p = item->path; *p; p++)
                if (*p == '/') name = p + 1;
            ino = item->ino;
            dtype = bootfs_is_dir(item) ? 4 : bootfs_is_link(item) ? 10 : 8;
        }
        unsigned len = 0;
        while (name[len]) len++;
        unsigned reclen = (19 + len + 1 + 7) & ~7U;
        if (reclen > capacity - written) {
            if (!written) return (uint64_t)-EINVAL;
            break;
        }
        uint8_t record[280];
        for (unsigned i = 0; i < reclen; i++) record[i] = 0;
        for (unsigned i = 0; i < 8; i++) {
            record[i] = (uint8_t)(ino >> (i * 8));
            record[8 + i] = (uint8_t)((cursor + 1) >> (i * 8));
        }
        record[16] = (uint8_t)reclen;
        record[17] = (uint8_t)(reclen >> 8);
        record[18] = (uint8_t)dtype;
        for (unsigned i = 0; i < len; i++) record[19 + i] = (uint8_t)name[i];
        if (!nas_user_write_bytes(dst + written, record, reclen))
            return written ? written : (uint64_t)-EFAULT;
        written += reclen;
        cursor++;
        nas_open_files[TASK.fds->entries[fd].slot].position = cursor;
    }
    return written;
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
