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
#include "vfs.h"
#define TASK (*nas_current_task)
/* Linux MAX_RW_COUNT: apply one transfer ceiling consistently to scalar
 * and vector read/write, pread/pwrite, independent of filesystem kind. */
#define NAS_MAX_RW_COUNT 0x7ffff000ULL
/* Owner-independent readiness. No Task pointer, process FD number or
 * userspace memory access is needed to poll a live open-file description.
 * Each backend may implement its own readiness semantics. */
uint16_t nas_fs_poll_description(unsigned slot,uint16_t events) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount)
        return 0;
    const struct nas_file_ops *ops=nas_open_files[slot].ops;
    return ops && ops->poll_description ?
           ops->poll_description(slot,events) : 0;
}

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
    const struct nas_open_file *opened=&nas_open_files[slot];
    if(opened->vnode_pinned) {
        const struct nas_fs_node *node=&opened->vnode;
        if(node->is_dir || !node->filesystem || !node->filesystem->read_at)
            return -ENODEV;
        if(!buffer && length)return -EFAULT;
        return nas_vfs_read_at(node,offset,buffer,length);
    }
    /* Legacy non-VFS Limine module FD only; migrated mounted providers
     * never branch by backend in the kernel-buffer data path. */
    if(TASK.fds->entries[fd].kind!=FD_BOOTFS)return -ENODEV;
    const struct limine_file *file=opened->boot_file;
    if(!file || !file->address)return -EBADF;
    if(offset>=file->size)return 0;
    if(length>file->size-offset)length=file->size-offset;
    if(!buffer && length)return -EFAULT;
    if(length)memcpy(buffer,(const uint8_t *)file->address+offset,(size_t)length);
    return (int64_t)length;
}

uint64_t nas_fs_fd_write(uint64_t fd, uint64_t address, uint64_t length) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)return (uint64_t)-EBADF;
    unsigned slot=TASK.fds->entries[fd].slot;
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount)
        return (uint64_t)-EBADF;
    /* O_PATH is never a writable description, irrespective of O_ACCMODE.
     * A combined mask would accidentally accept it as a write permission. */
    if((nas_open_files[slot].status_flags & NAS_LINUX_O_PATH) ||
       (nas_open_files[slot].status_flags & 3ULL)==0ULL)
        return (uint64_t)-EBADF;
    /* The VFS access_ok check precedes provider dispatch, even for a
     * non-copying sink, zero-length write or provider-rejected request.
     * This checks the address RANGE, not whether its pages are mapped. */
    if(address>=USER_LIMIT || length>USER_LIMIT-address)
        return (uint64_t)-EFAULT;
    /* Linux bounds one regular write to MAX_RW_COUNT (INT_MAX rounded down
     * to the page size), not a kernel-private 1-MiB limit. GNU binutils
     * legitimately calls writev() with a single multi-megabyte iovec.
     * The file/device ops still copy in bounded chunks and may short-write. */
    if(length>NAS_MAX_RW_COUNT)length=NAS_MAX_RW_COUNT;
    const struct nas_file_ops *ops=nas_open_files[slot].ops;
    if(!ops || !ops->write)return (uint64_t)-EBADF;
    /* The provider authorizes the ORIGINAL logical request before any
     * usercopy/chunking. For pipes, <= PIPE_BUF must fit as one unit. */
    uint64_t budget=ops->write_plan ? ops->write_plan(slot,length) : length;
    if((int64_t)budget<0)return budget;
    if(budget>length)return (uint64_t)-EIO;
    if(!budget)return 0;
    if(ops->write_ignores_source) {
        /* /dev/null and /dev/zero are sinks: no userspace byte is read.
         * Absent pages within a valid user range do not imply EFAULT. */
        uint64_t discarded=ops->write(slot,0,budget);
        if((int64_t)discarded<0)return discarded;
        if(discarded>budget)return (uint64_t)-EIO;
        return discarded;
    }
    /* Only the provider-authorized byte count is DEREFERENCED; an eventfd
     * rejected write never copies or consumes the user's source bytes. */
    if(ops->atomic_write_limit && length<=ops->atomic_write_limit &&
       !nas_user_prepare_read(address,budget))
        return (uint64_t)-EFAULT;
    uint8_t buffer[1024];
    uint64_t done=0;
    while(done<budget) {
        uint64_t n=budget-done;
        uint64_t room=4096ULL-((address+done)&4095ULL);
        if(n>sizeof(buffer))n=sizeof(buffer);
        /* Tiny indivisible providers (eventfd: eight bytes) must receive
         * one complete kernel buffer even across a user page boundary. */
        if(n>room && budget>8)n=room;
        if(!nas_user_read_bytes(address+done,buffer,n))
            return done ? done : (uint64_t)-EFAULT;
        uint64_t written=ops->write(slot,buffer,n);
        if((int64_t)written<0)return done ? done : written;
        if(written>n)return done ? done : (uint64_t)-EIO;
        done+=written;
        if(written<n)break;
    }
    return done;
}
uint64_t nas_fs_fd_read(uint64_t fd, uint64_t address, uint64_t length) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)return (uint64_t)-EBADF;
    unsigned slot=TASK.fds->entries[fd].slot;
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount)
        return (uint64_t)-EBADF;
    if((nas_open_files[slot].status_flags&NAS_LINUX_O_PATH) ||
       (nas_open_files[slot].status_flags&3ULL)==1)
        return (uint64_t)-EBADF;
    /* Even an EOF read must reject an out-of-range pointer. A merely
     * unmapped pointer is allowed when the provider returns zero bytes. */
    if(address>=USER_LIMIT || length>USER_LIMIT-address)
        return (uint64_t)-EFAULT;
    if(length>NAS_MAX_RW_COUNT)length=NAS_MAX_RW_COUNT;
    const struct nas_file_ops *ops=nas_open_files[slot].ops;
    if(!ops || !ops->read)return (uint64_t)-EBADF;
    uint64_t budget=ops->read_plan ? ops->read_plan(slot,length) : length;
    if((int64_t)budget<0)return budget;
    if(budget>length)return (uint64_t)-EIO;
    if(!budget) {
        /* A planned zero-byte EOF can be an input token (PTY canonical
         * terminal). It still needs a zero-length provider commit. */
        if(length && ops->read_finalize_zero) {
            uint8_t dummy=0;
            uint64_t rc=ops->read(slot,&dummy,0);
            if((int64_t)rc<0)return rc;
            if(rc)return (uint64_t)-EIO;
        }
        return 0;
    }
    uint8_t buffer[1024];
    uint64_t done=0;
    while(done<budget) {
        uint64_t n=budget-done;
        uint64_t room=4096ULL-((address+done)&4095ULL);
        if(n>sizeof(buffer))n=sizeof(buffer);
        if(n>room && budget>8)n=room;
        /* In particular, a consuming Pipe/PTY/TTY/Eventfd provider must
         * not discard data or advance its cursor before this succeeds. */
        if(!ops->read_commit_before_copyout &&
           !nas_user_prepare_write(address+done,n))
            return done ? done : (uint64_t)-EFAULT;
        uint64_t got=ops->read(slot,buffer,n);
        if((int64_t)got<0)return done ? done : got;
        if(got>n)return done ? done : (uint64_t)-EIO;
        if(!got)break;
        if(!nas_user_write_bytes(address+done,buffer,got))
            return done ? done : (uint64_t)-EFAULT;
        done+=got;
        if(got<n)break;
    }
    return done;
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
    /* Inspect the opened VFS provider capability, not an enumerated list
     * of specific mounted filesystem FD kinds. */
    const struct nas_open_file *read_open=&nas_open_files[slot];
    if(read_open->vnode_pinned) {
        if(read_open->vnode.is_dir)return (uint64_t)-EISDIR;
        if(!read_open->vnode.filesystem ||
           !read_open->vnode.filesystem->read_at)return (uint64_t)-ESPIPE;
    } else if(TASK.fds->entries[fd].kind!=FD_BOOTFS)
        return (uint64_t)-ESPIPE;
    if(offset>INT64_MAX)return (uint64_t)-EINVAL;
    if(count>NAS_MAX_RW_COUNT)count=NAS_MAX_RW_COUNT;
    if(count>INT64_MAX-offset)return (uint64_t)-EINVAL;
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
    const struct nas_open_file *write_open=&nas_open_files[slot];
    if(!write_open->vnode_pinned)return (uint64_t)-ESPIPE;
    if(write_open->vnode.is_dir)return (uint64_t)-EISDIR;
    if(!write_open->vnode.filesystem ||
       !write_open->vnode.filesystem->write_at)return (uint64_t)-ESPIPE;
    if(offset>INT64_MAX)return (uint64_t)-EINVAL;
    if(count>NAS_MAX_RW_COUNT)count=NAS_MAX_RW_COUNT;
    if(count>INT64_MAX-offset)return (uint64_t)-EINVAL;
    if(count && (address>=USER_LIMIT || count>USER_LIMIT-address))
        return (uint64_t)-EFAULT;
    struct nas_open_file *opened=&nas_open_files[slot];
    if(!opened->vnode_pinned)return (uint64_t)-EBADF;
    if(!nas_vfs_can_write(&opened->vnode))return (uint64_t)-EROFS;
    uint64_t done=0;
    uint8_t buffer[1024];
    while(done<count) {
        uint64_t n=count-done;
        if(n>sizeof(buffer))n=sizeof(buffer);
        if(!nas_user_read_bytes(address+done,buffer,n))
            return done ? done : (uint64_t)-EFAULT;
        int64_t wrote=nas_vfs_write_at(&opened->vnode,offset+done,buffer,n);
        if(wrote<0)return done ? done : (uint64_t)wrote;
        done+=(uint64_t)wrote;
        if(offset+done>opened->vnode.size)opened->vnode.size=offset+done;
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
        /* A vector's aggregate result, not just each read(), is capped.
         * Stop after the cap without issuing another usercopy/provider IO. */
        if(total==NAS_MAX_RW_COUNT)break;
        uint64_t allowed=bytes;
        if(allowed>NAS_MAX_RW_COUNT-total)
            allowed=NAS_MAX_RW_COUNT-total;
        /* The common read owns the planned-byte address check. A vector
         * may describe more storage than a provider can actually return
         * (eventfd: eight bytes; regular-file EOF: zero bytes). */
        uint64_t received=nas_fs_fd_read(fd,address,allowed);
        if ((int64_t)received<0) return total ? total : received;
        total+=received;
        if (received!=allowed || allowed!=bytes) break;
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
        if (allowed > NAS_MAX_RW_COUNT - sent)
            allowed = NAS_MAX_RW_COUNT - sent;
        if (!allowed) break;
        uint64_t r = nas_fs_fd_write(fd, address, allowed);
        if ((int64_t)r < 0) return sent ? sent : r;
        sent += r;
        if (r != allowed || allowed != bytes) break;
    }
    return sent;
}
