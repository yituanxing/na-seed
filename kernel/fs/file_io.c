/* Common descriptor data-plane and kernel-buffer MAP_PRIVATE reads. */
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
#include "file_ops.h"
#define TASK (*nas_current_task)

/* Same open-file-description source for read(), exec and private mmap.
 * Do not expose kernel pointer paths or share mutable file offsets. */
int64_t nas_fs_mmap_read(uint64_t fd, uint64_t offset,
                        uint8_t *buffer, uint64_t length) {
    if (fd >= NAS_FD_LIMIT || TASK.fds->entries[fd].kind == FD_CLOSED)
        return -EBADF;
    unsigned slot = TASK.fds->entries[fd].slot;
    if (slot >= NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount)
        return -EBADF;
    if(nas_open_files[slot].status_flags & NAS_LINUX_O_PATH)return -EBADF;
    if ((nas_open_files[slot].status_flags & 3ULL) == 1ULL)
        return -EACCES;
    const uint8_t *bytes = 0;
    uint64_t size = 0;
    if (TASK.fds->entries[fd].kind == FD_ARCHIVE) {
        const struct bootfs_entry *entry = nas_open_files[slot].boot_entry;
        if (!entry || bootfs_is_dir(entry)) return -ENODEV;
        bytes = entry->data;
        size = entry->size;
    } else if (TASK.fds->entries[fd].kind == FD_BOOTFS) {
        const struct limine_file *file = nas_open_files[slot].boot_file;
        if (!file || !file->address) return -EBADF;
        bytes = (const uint8_t *)file->address;
        size = file->size;
    } else if (TASK.fds->entries[fd].kind == FD_TMP) {
        const struct nas_tmp_file *file = tmp_by_slot(slot);
        if (!file || file->is_dir) return -ENODEV;
        return tmp_read_at(file, offset, buffer, length);
    } else {
        return -ENODEV;
    }
    if (offset >= size) return 0;
    if (length > size-offset) length = size-offset;
    if (length && !buffer) return -EFAULT;
    if (length) memcpy(buffer, bytes+offset, (size_t)length);
    return (int64_t)length;
}

uint64_t nas_fs_fd_write(uint64_t fd, uint64_t address, uint64_t length) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)return (uint64_t)-EBADF;
    if((nas_open_files[TASK.fds->entries[fd].slot].status_flags & (NAS_LINUX_O_PATH|3ULL))==0)
        return (uint64_t)-EBADF;
    /* Linux bounds one regular write to MAX_RW_COUNT (INT_MAX rounded down
     * to the page size), not a kernel-private 1-MiB limit. GNU binutils
     * legitimately calls writev() with a single multi-megabyte iovec.
     * The file/device ops still copy in bounded chunks and may short-write. */
    if(length>0x7ffff000ULL)length=0x7ffff000ULL;
    if(address>=USER_LIMIT || length>USER_LIMIT-address)
        return (uint64_t)-EFAULT;
    const struct nas_file_ops *ops=nas_open_files[TASK.fds->entries[fd].slot].ops;
    return ops && ops->write ? ops->write(fd,address,length) : (uint64_t)-EBADF;
}
uint64_t nas_fs_fd_read(uint64_t fd, uint64_t address, uint64_t length) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)return (uint64_t)-EBADF;
    if((nas_open_files[TASK.fds->entries[fd].slot].status_flags&NAS_LINUX_O_PATH) ||
       (nas_open_files[TASK.fds->entries[fd].slot].status_flags&3ULL)==1)
        return (uint64_t)-EBADF;
    const struct nas_file_ops *ops=nas_open_files[TASK.fds->entries[fd].slot].ops;
    return ops && ops->read ? ops->read(fd,address,length) : (uint64_t)-EBADF;
}
/* The Linux positional I/O ABI is the same common VFS operation on all
 * three architectures. Respect per-FD access mode and reject pipes,
 * devices and directories with ESPIPE/EISDIR rather than pretending to
 * read a regular file. Keep offset independent of fork/dup shared state.
 * The same kernel-buffer regular-file reader already powers mmap. */
uint64_t nas_fs_fd_pread(uint64_t fd,uint64_t address,uint64_t count,
                         uint64_t offset) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED) return (uint64_t)-EBADF;
    unsigned slot=TASK.fds->entries[fd].slot;
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount) return (uint64_t)-EBADF;
    if((nas_open_files[slot].status_flags&NAS_LINUX_O_PATH) ||
       (nas_open_files[slot].status_flags&3ULL)==1ULL) return (uint64_t)-EBADF;
    if(TASK.fds->entries[fd].kind==FD_DIR || TASK.fds->entries[fd].kind==FD_TMP_DIR) return (uint64_t)-EISDIR;
    if(TASK.fds->entries[fd].kind!=FD_TMP && TASK.fds->entries[fd].kind!=FD_ARCHIVE &&
       TASK.fds->entries[fd].kind!=FD_BOOTFS) return (uint64_t)-ESPIPE;
    if(offset>INT64_MAX || count>INT64_MAX-offset) return (uint64_t)-EINVAL;
    if(count && (address>=USER_LIMIT || count>USER_LIMIT-address))
        return (uint64_t)-EFAULT;
    uint8_t buffer[1024];
    uint64_t done=0;
    while(done<count) {
        uint64_t take=count-done;
        if(take>sizeof(buffer))take=sizeof(buffer);
        int64_t got=nas_fs_mmap_read(fd,offset+done,buffer,take);
        if(got<0)return done ? done : (uint64_t)got;
        if(!got)break;
        if(!nas_user_write_bytes(address+done,buffer,(uint64_t)got))
            return done ? done : (uint64_t)-EFAULT;
        done+=(uint64_t)got;
        if((uint64_t)got<take)break;
    }
    return done;
}
uint64_t nas_fs_fd_pwrite(uint64_t fd,uint64_t address,uint64_t count,
                          uint64_t offset) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED) return (uint64_t)-EBADF;
    unsigned slot=TASK.fds->entries[fd].slot;
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount) return (uint64_t)-EBADF;
    if((nas_open_files[slot].status_flags&NAS_LINUX_O_PATH) ||
       (nas_open_files[slot].status_flags&3ULL)==0ULL) return (uint64_t)-EBADF;
    if(TASK.fds->entries[fd].kind==FD_DIR || TASK.fds->entries[fd].kind==FD_TMP_DIR) return (uint64_t)-EISDIR;
    if(TASK.fds->entries[fd].kind!=FD_TMP)return (uint64_t)-ESPIPE;
    if(offset>INT64_MAX || count>INT64_MAX-offset)return (uint64_t)-EINVAL;
    if(count && (address>=USER_LIMIT || count>USER_LIMIT-address))
        return (uint64_t)-EFAULT;
    struct nas_tmp_file *file=tmp_by_slot(slot);
    if(!file)return (uint64_t)-EBADF;
    uint64_t done=0;
    uint8_t buffer[1024];
    while(done<count) {
        uint64_t n=count-done;
        if(n>sizeof(buffer))n=sizeof(buffer);
        if(!nas_user_read_bytes(address+done,buffer,n))
            return done ? done : (uint64_t)-EFAULT;
        int64_t wrote=tmp_write_at(file,offset+done,buffer,n);
        if(wrote<0)return done ? done : (uint64_t)wrote;
        done+=(uint64_t)wrote;
        if((uint64_t)wrote<n)break;
    }
    return done;
}
/* Linux readv LP64 iovec. All slices share the open description's offset.
 * A partial transfer takes priority over an error. For an empty pipe the
 * standard internal wait sentinel returns without touching any buffer;
 * the blocked syscall is replayed only in its owner's user VM. */
uint64_t nas_fs_fd_readv(uint64_t fd,uint64_t iov,uint64_t count) {
    if (count > 64) return (uint64_t)-EINVAL;
    if (count && (iov >= USER_LIMIT || count > (USER_LIMIT-iov)/16))
        return (uint64_t)-EFAULT;
    uint64_t total=0;
    for (uint64_t n=0;n<count;n++) {
        uint64_t address,bytes;
        if (!nas_user_read_u64(iov+n*16,&address) ||
            !nas_user_read_u64(iov+n*16+8,&bytes))
            return total ? total : (uint64_t)-EFAULT;
        if (bytes>INT64_MAX-total) return (uint64_t)-EINVAL;
        if (!bytes) continue;
        if (address>=USER_LIMIT || bytes>USER_LIMIT-address)
            return total ? total : (uint64_t)-EFAULT;
        uint64_t received=nas_fs_fd_read(fd,address,bytes);
        if ((int64_t)received<0) return total ? total : received;
        total+=received;
        if (received!=bytes) break;
    }
    return total;
}
uint64_t nas_fs_fd_writev(uint64_t fd, uint64_t iov, uint64_t count) {
    if (count > 64) return (uint64_t)-EINVAL;
    if (count && (iov >= USER_LIMIT || count > (USER_LIMIT - iov) / 16))
        return (uint64_t)-EFAULT;
    uint64_t sent = 0;
    for (uint64_t n = 0; n < count; n++) {
        uint64_t address, bytes;
        if (!nas_user_read_u64(iov + n * 16, &address) ||
            !nas_user_read_u64(iov + n * 16 + 8, &bytes))
            return sent ? sent : (uint64_t)-EFAULT;
        /* The Linux total transfer limit applies across the entire iovec,
         * and the shared write path enforces pointer/FD validity. */
        uint64_t allowed = bytes;
        if (allowed > 0x7ffff000ULL - sent)
            allowed = 0x7ffff000ULL - sent;
        if (!allowed) break;
        uint64_t r = nas_fs_fd_write(fd, address, allowed);
        if ((int64_t)r < 0) return sent ? sent : r;
        sent += r;
        if (r != allowed || allowed != bytes) break;
    }
    return sent;
}
