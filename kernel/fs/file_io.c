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
#include "../mm/heap.h"
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
/* LP64 Linux iovec data plane. Every iovec is imported once into heap-owned
 * kernel memory BEFORE any provider can consume data or change File offsets.
 * 1024 entries occupy 16 KiB, so never place the array on a kernel stack. */
#define NAS_IOV_MAX 1024ULL
struct nas_io_vector { uint64_t address,length; };
struct nas_io_cursor { uint64_t index,offset; };
struct nas_io_request {
    struct nas_io_vector *vectors;
    uint64_t count,total;
    unsigned slot;
    const struct nas_file_ops *ops;
};
static void io_request_release(struct nas_io_request *req) {
    if(req->vectors)(void)nas_heap_free(req->vectors);
    req->vectors=0;
}
static int io_request_import(uint64_t fd,uint64_t user_iov,uint64_t count,
                             bool writing,struct nas_io_request *req) {
    *req=(struct nas_io_request){0};
    /* Linux looks up and checks the File before importing user iovecs,
     * including an empty iovec array. This also prevents zero-byte writev
     * from silently succeeding on an O_RDONLY/open(O_PATH) descriptor. */
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)
        return -EBADF;
    unsigned slot=TASK.fds->entries[fd].slot;
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount)
        return -EBADF;
    uint64_t flags=nas_open_files[slot].status_flags;
    if((flags&NAS_LINUX_O_PATH) ||
       (writing ? (flags&3ULL)==0 : (flags&3ULL)==1))
        return -EBADF;
    const struct nas_file_ops *ops=nas_open_files[slot].ops;
    if(!ops || (writing ? !ops->write : !ops->read))return -EBADF;
    if(count>NAS_IOV_MAX)return -EINVAL;
    if(count && (user_iov>=USER_LIMIT ||
                 count>(USER_LIMIT-user_iov)/16ULL))return -EFAULT;
    struct nas_io_vector *vectors=0;
    if(count) {
        vectors=nas_heap_alloc((size_t)count*sizeof(*vectors));
        if(!vectors)return -ENOMEM;
    }
    /* Linux validates each signed iov_len while copying the complete
     * metadata array, before it checks any individual data range. */
    for(uint64_t i=0;i<count;i++) {
        uint64_t address,length;
        if(!nas_user_read_u64(user_iov+i*16,&address) ||
           !nas_user_read_u64(user_iov+i*16+8,&length)) {
            (void)nas_heap_free(vectors);
            return -EFAULT;
        }
        if(length>INT64_MAX) {
            (void)nas_heap_free(vectors);
            return -EINVAL;
        }
        vectors[i]=(struct nas_io_vector){address,length};
    }
    uint64_t total=0;
    for(uint64_t i=0;i<count;i++) {
        uint64_t address=vectors[i].address;
        uint64_t length=vectors[i].length;
        /* Linux's single-iovec ITER_UBUF fast path caps length BEFORE
         * access_ok; multi-iovec import checks each ORIGINAL data range,
         * including segments beyond the eventual MAX_RW_COUNT ceiling. */
        uint64_t checked=length;
        if(count==1 && checked>NAS_MAX_RW_COUNT)checked=NAS_MAX_RW_COUNT;
        if(address>=USER_LIMIT || checked>USER_LIMIT-address) {
            (void)nas_heap_free(vectors);
            return -EFAULT;
        }
        if(count==1)vectors[i].length=checked;
        uint64_t remaining=NAS_MAX_RW_COUNT-total;
        total+=length<remaining ? length:remaining;
    }
    *req=(struct nas_io_request){vectors,count,total,slot,ops};
    return 0;
}
/* Advance a cursor across empty iovecs and return its next user slice.
 * Callers hold a private cursor while preflighting to avoid changing the
 * committed transfer cursor before an operation is accepted. */
static bool io_next_slice(const struct nas_io_request *req,
                          struct nas_io_cursor *cursor,uint64_t maximum,
                          uint64_t *address,uint64_t *length) {
    while(cursor->index<req->count &&
          cursor->offset==req->vectors[cursor->index].length) {
        cursor->index++;
        cursor->offset=0;
    }
    if(cursor->index>=req->count)return false;
    const struct nas_io_vector *v=&req->vectors[cursor->index];
    uint64_t take=v->length-cursor->offset;
    if(take>maximum)take=maximum;
    *address=v->address+cursor->offset;
    *length=take;
    cursor->offset+=take;
    return true;
}
static bool io_prepare_vector(const struct nas_io_request *req,
                              struct nas_io_cursor cursor,uint64_t length,
                              bool writing) {
    while(length) {
        uint64_t address,take;
        if(!io_next_slice(req,&cursor,length,&address,&take))return false;
        if(writing ? !nas_user_prepare_read(address,take) :
                     !nas_user_prepare_write(address,take))return false;
        length-=take;
    }
    return true;
}
/* read_iter must retain Linux's partial copy-out semantics. If a later
 * destination iovec (or page) is unmapped, previously writable slices may
 * still be returned as a successful short read; never reject all admitted
 * bytes just because a later destination cannot be faulted in. In contrast,
 * eventfd's read_commit_before_copyout explicitly consumes before copyout. */
static uint64_t io_writable_prefix(const struct nas_io_request *req,
                                   struct nas_io_cursor cursor,
                                   uint64_t requested) {
    uint64_t ready=0;
    while(ready<requested) {
        uint64_t addr,n;
        if(!io_next_slice(req,&cursor,requested-ready,&addr,&n))break;
        /* Probe page-granular slices so that one bad page does not erase
         * progress into valid earlier pages of the SAME iovec. */
        uint64_t offset=0;
        while(offset<n) {
            uint64_t take=n-offset;
            uint64_t room=4096ULL-((addr+offset)&4095ULL);
            if(take>room)take=room;
            if(!nas_user_prepare_write(addr+offset,take))return ready;
            ready+=take;
            offset+=take;
        }
    }
    return ready;
}
static bool io_gather_vector(const struct nas_io_request *req,
                             struct nas_io_cursor *cursor,uint8_t *dst,
                             uint64_t length) {
    uint64_t done=0;
    while(done<length) {
        uint64_t address,take;
        if(!io_next_slice(req,cursor,length-done,&address,&take) ||
           !nas_user_read_bytes(address,dst+done,take))return false;
        done+=take;
    }
    return true;
}
static bool io_scatter_vector(const struct nas_io_request *req,
                              struct nas_io_cursor *cursor,const uint8_t *src,
                              uint64_t length,uint64_t *copied) {
    *copied=0;
    while(*copied<length) {
        uint64_t address,take;
        if(!io_next_slice(req,cursor,length-*copied,&address,&take) ||
           !nas_user_write_bytes(address,src+*copied,take))return false;
        *copied+=take;
    }
    return true;
}
static uint64_t io_aggregate_read(const struct nas_io_request *req) {
    uint64_t requested=req->total;
    if(requested>NAS_MAX_RW_COUNT)requested=NAS_MAX_RW_COUNT;
    uint64_t budget=req->ops->read_plan ?
        req->ops->read_plan(req->slot,requested):requested;
    if((int64_t)budget<0)return budget;
    if(budget>requested)return (uint64_t)-EIO;
    if(!budget) {
        if(requested && req->ops->read_finalize_zero) {
            uint8_t dummy=0;
            uint64_t rc=req->ops->read(req->slot,&dummy,0);
            if((int64_t)rc<0)return rc;
            if(rc)return (uint64_t)-EIO;
        }
        return 0;
    }
    struct nas_io_cursor cursor={0};
    uint8_t buffer[1024];
    uint64_t done=0;
    while(done<budget) {
        uint64_t n=budget-done;
        if(n>sizeof(buffer))n=sizeof(buffer);
        /* Preflight only the WRITEABLE PREFIX. Linux readv can complete a
         * valid first iovec even when a later one faults. Never consume data
         * beyond that prefix. Eventfd explicitly opts out and consumes the
         * counter before copy_to_iter, even if its copyout then faults. */
        if(!req->ops->read_commit_before_copyout) {
            n=io_writable_prefix(req,cursor,n);
            if(!n)return done ? done : (uint64_t)-EFAULT;
        }
        uint64_t got=req->ops->read(req->slot,buffer,n);
        if((int64_t)got<0)return done ? done : got;
        if(got>n)return done ? done : (uint64_t)-EIO;
        if(!got)break;
        uint64_t copied=0;
        if(!io_scatter_vector(req,&cursor,buffer,got,&copied)) {
            /* eventfd has already decremented its counter; Linux reports
             * EFAULT, not a successful partial eight-byte read. */
            if(req->ops->read_commit_before_copyout)return (uint64_t)-EFAULT;
            return done+copied ? done+copied : (uint64_t)-EFAULT;
        }
        done+=got;
        if(got<n)break;
    }
    return done;
}
static uint64_t io_aggregate_write(const struct nas_io_request *req) {
    uint64_t requested=req->total;
    if(requested>NAS_MAX_RW_COUNT)requested=NAS_MAX_RW_COUNT;
    uint64_t budget=req->ops->write_plan ?
        req->ops->write_plan(req->slot,requested):requested;
    if((int64_t)budget<0)return budget;
    if(budget>requested)return (uint64_t)-EIO;
    if(!budget)return 0;
    if(req->ops->write_ignores_source) {
        uint64_t discarded=req->ops->write(req->slot,0,budget);
        if((int64_t)discarded<0)return discarded;
        return discarded<=budget ? discarded:(uint64_t)-EIO;
    }
    /* A PIPE_BUF-sized writev is ONE transaction regardless of how many
     * iovecs it contains. Resolve every source page before any commit. */
    if(req->ops->atomic_write_limit &&
       requested<=req->ops->atomic_write_limit &&
       !io_prepare_vector(req,(struct nas_io_cursor){0},budget,true))
        return (uint64_t)-EFAULT;
    struct nas_io_cursor cursor={0};
    uint8_t buffer[1024];
    uint64_t done=0;
    while(done<budget) {
        uint64_t n=budget-done;
        if(n>sizeof(buffer))n=sizeof(buffer);
        if(!io_gather_vector(req,&cursor,buffer,n))
            return done ? done:(uint64_t)-EFAULT;
        uint64_t wrote=req->ops->write(req->slot,buffer,n);
        if((int64_t)wrote<0)return done ? done:wrote;
        if(wrote>n)return done ? done:(uint64_t)-EIO;
        done+=wrote;
        if(wrote<n)break;
    }
    return done;
}
/* Legacy scalar callback fallback. The metadata has ALREADY been imported
 * and verified, avoiding the old bug where a later invalid iovec caused
 * partial pipe writes/consumption before Linux's import_iovec EFAULT. */
static uint64_t io_segmented_read(uint64_t fd,const struct nas_io_request *req) {
    uint64_t done=0;
    for(uint64_t i=0;i<req->count && done<NAS_MAX_RW_COUNT;i++) {
        uint64_t bytes=req->vectors[i].length;
        if(!bytes)continue;
        if(bytes>NAS_MAX_RW_COUNT-done)bytes=NAS_MAX_RW_COUNT-done;
        uint64_t got=nas_fs_fd_read(fd,req->vectors[i].address,bytes);
        if((int64_t)got<0)return done ? done:got;
        done+=got;
        if(got!=bytes || bytes!=req->vectors[i].length)break;
    }
    return done;
}
static uint64_t io_segmented_write(uint64_t fd,const struct nas_io_request *req) {
    uint64_t done=0;
    for(uint64_t i=0;i<req->count && done<NAS_MAX_RW_COUNT;i++) {
        uint64_t bytes=req->vectors[i].length;
        if(!bytes) {
            /* A leading zero-length segment is handed to a scalar-only
             * provider when later data exists: Linux eventfd.write(0)
             * rejects [0,8], but skips zero entries after progress. */
            if(!done && req->total) {
                uint64_t rc=nas_fs_fd_write(fd,req->vectors[i].address,0);
                if((int64_t)rc<0)return rc;
            }
            continue;
        }
        if(bytes>NAS_MAX_RW_COUNT-done)bytes=NAS_MAX_RW_COUNT-done;
        uint64_t wrote=nas_fs_fd_write(fd,req->vectors[i].address,bytes);
        if((int64_t)wrote<0)return done ? done:wrote;
        done+=wrote;
        if(wrote!=bytes || bytes!=req->vectors[i].length)break;
    }
    return done;
}
/* The issuing syscall may park after -EAGAIN. Return the already-imported
 * logical request size as a kernel-owned wait hint: the scheduler must NEVER
 * dereference a different task's iovec pointer just to check TTY readiness. */
uint64_t nas_fs_fd_readv_with_size(uint64_t fd,uint64_t iov,uint64_t count,
                                  uint64_t *requested_out) {
    if(requested_out)*requested_out=0;
    struct nas_io_request req;
    int rc=io_request_import(fd,iov,count,false,&req);
    if(rc)return (uint64_t)(int64_t)rc;
    if(requested_out)*requested_out=req.total;
    uint64_t result=!req.total ? 0 :
        req.ops->aggregate_readv ? io_aggregate_read(&req) :
                                   io_segmented_read(fd,&req);
    io_request_release(&req);
    return result;
}
uint64_t nas_fs_fd_readv(uint64_t fd,uint64_t iov,uint64_t count) {
    return nas_fs_fd_readv_with_size(fd,iov,count,0);
}
uint64_t nas_fs_fd_writev(uint64_t fd,uint64_t iov,uint64_t count) {
    struct nas_io_request req;
    int rc=io_request_import(fd,iov,count,true,&req);
    if(rc)return (uint64_t)(int64_t)rc;
    uint64_t result=!req.total ? 0 :
        req.ops->aggregate_writev ? io_aggregate_write(&req) :
                                    io_segmented_write(fd,&req);
    io_request_release(&req);
    return result;
}
