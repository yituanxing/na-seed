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
#include "../drivers/tty.h"
#include "../arch/arch.h"
#include "../arch/mmu.h"
#include "../arch/linux_fcntl_abi.h"
#include "../syscall/ops.h"
#include "io.h"
#include "file_ops.h"
#include "syscall.h"
#include "vfs.h"
#define TASK (*nas_current_task)
/* Linux MAX_RW_COUNT: apply one transfer ceiling consistently to scalar
 * and vector read/write, pread/pwrite, independent of filesystem kind. */
#define NAS_MAX_RW_COUNT 0x7ffff000ULL
/* A signalfd's pending queue belongs to the reader's Task; all other
 * providers remain open-description owned. Scheduler probes are explicitly
 * passed the blocked Task, never made against another current Task. */
uint16_t nas_fs_poll_description_task(unsigned slot,uint16_t events,
                                     const struct nas_task *task) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount)
        return 0;
    const struct nas_file_ops *ops=nas_open_files[slot].ops;
    if(ops==nas_fs_ops_for_kind(FD_SIGNALFD))
        return nas_fs_signalfd_poll_task(slot,events,task);
    return ops && ops->poll_description ?
           ops->poll_description(slot,events) : 0;
}
uint16_t nas_fs_poll_description(unsigned slot,uint16_t events) {
    return nas_fs_poll_description_task(slot,events,nas_current_task);
}

/* Same open-file-description source for read(), exec and file mmap.
 * Do not expose kernel pointer paths or share mutable file offsets. */
int nas_fs_mmap_immutable_readonly(uint64_t fd) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)
        return -EBADF;
    unsigned slot=TASK.fds->entries[fd].slot;
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount)
        return -EBADF;
    const struct nas_open_file *opened=&nas_open_files[slot];
    if(opened->status_flags & NAS_LINUX_O_PATH)return -EBADF;
    if((opened->status_flags & 3ULL)==1ULL)return -EACCES;
    if(opened->vnode_pinned) {
        const struct nas_fs_node *node=&opened->vnode;
        if(node->is_dir || !node->filesystem || !node->filesystem->read_at)
            return -ENODEV;
        /* A read-only procfs generator is NOT immutable; neither is a
         * read-only mount of a potentially writable filesystem. */
        return node->filesystem->read_only &&
               node->filesystem->immutable_file_data ? 1 : 0;
    }
    /* Legacy direct Limine modules are permanently resident boot data. */
    if(TASK.fds->entries[fd].kind==FD_BOOTFS && opened->boot_file &&
       opened->boot_file->address)
        return 1;
    return 0;
}
/* One mapped File identity, separately owned from its creating descriptor.
 * The VFS inode/mount lease survives FD close, rename and final unlink;
 * VMA splits/fork share the counted object rather than taking unrelated
 * pathname snapshots. No per-provider inode fields escape this adapter. */
struct nas_mapped_file {
    uint32_t refs;
    bool write_allowed;
    struct nas_fs_node node;
};
int nas_fs_mmap_pin(uint64_t fd,bool writable,struct nas_mapped_file **out) {
    if(!out)return -EINVAL;
    *out=NULL;
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)
        return -EBADF;
    unsigned slot=TASK.fds->entries[fd].slot;
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount)
        return -EBADF;
    const struct nas_open_file *opened=&nas_open_files[slot];
    if(opened->status_flags & NAS_LINUX_O_PATH)return -EBADF;
    const unsigned access=(unsigned)(opened->status_flags&3ULL);
    /* Linux requires a readable descriptor for every file mmap, and an
     * O_RDWR descriptor for writable MAP_SHARED, even if the original
     * PROT_READ mapping requests write permission only via mprotect later. */
    if(access==1U || access==3U)return -EACCES;
    if(writable && access!=2U)return -EACCES;
    if(!opened->vnode_pinned)return -ENODEV;
    const struct nas_fs_node *node=&opened->vnode;
    if(node->is_dir || (node->mode&0170000U)!=0100000U ||
       !node->filesystem || !node->filesystem->read_at)
        return -ENODEV;
    /* A provider without true shared physical pages is NOT equivalent to
     * a private eager copy. In particular, ext4 requires a coherent page
     * cache before participating. */
    if(!node->filesystem->acquire_shared_page)return -ENOSYS;
    if(writable && !nas_vfs_can_write(node))return -EACCES;
    struct nas_mapped_file *mapped=nas_heap_alloc(sizeof(*mapped));
    if(!mapped)return -ENOMEM;
    int rc=nas_vfs_node_pin(node);
    if(rc) {
        (void)nas_heap_free(mapped);
        return rc;
    }
    *mapped=(struct nas_mapped_file){.refs=1,.write_allowed=access==2U,
                                      .node=*node};
    *out=mapped;
    return 0;
}
/* A private lazy mapping must hold its ORIGINAL immutable inode, not a live
 * task FD (which musl closes immediately after mapping libLLVM). CPIO bootfs
 * does not implement shared physical pages, and should not have to. */
int nas_fs_mmap_pin_immutable(uint64_t fd,struct nas_mapped_file **out) {
    if(!out)return -EINVAL;
    *out=NULL;
    int immutable=nas_fs_mmap_immutable_readonly(fd);
    if(immutable<0)return immutable;
    if(!immutable)return -EOPNOTSUPP;
    unsigned slot=TASK.fds->entries[fd].slot;
    const struct nas_open_file *opened=&nas_open_files[slot];
    if(!opened->vnode_pinned)return -ENODEV;
    const struct nas_fs_node *node=&opened->vnode;
    if(node->is_dir || (node->mode&0170000U)!=0100000U ||
       !node->filesystem || !node->filesystem->read_at)return -ENODEV;
    struct nas_mapped_file *mapped=nas_heap_alloc(sizeof(*mapped));
    if(!mapped)return -ENOMEM;
    int rc=nas_vfs_node_pin(node);
    if(rc) {
        (void)nas_heap_free(mapped);
        return rc;
    }
    *mapped=(struct nas_mapped_file){.refs=1,.write_allowed=false,.node=*node};
    *out=mapped;
    return 0;
}
int64_t nas_fs_mmap_read_immutable(struct nas_mapped_file *file,
                                    uint64_t offset,uint8_t *dst,uint64_t len) {
    if(!file || !file->refs || !file->node.filesystem ||
       !file->node.filesystem->read_only ||
       !file->node.filesystem->immutable_file_data ||
       (!dst && len))return -EINVAL;
    return nas_vfs_read_at(&file->node,offset,dst,len);
}
bool nas_fs_mmap_get(struct nas_mapped_file *file) {
    if(!file || !file->refs || file->refs==UINT32_MAX)return false;
    ++file->refs;
    return true;
}
void nas_fs_mmap_put(struct nas_mapped_file *file) {
    if(!file || !file->refs)__builtin_trap();
    if(--file->refs)return;
    if(nas_vfs_node_unpin(&file->node))__builtin_trap();
    if(!nas_heap_free(file))__builtin_trap();
}
bool nas_fs_mmap_writable(const struct nas_mapped_file *file) {
    return file && file->refs && file->write_allowed &&
           nas_vfs_can_write(&file->node);
}
int nas_fs_mmap_page(struct nas_mapped_file *file,uint64_t offset,
                     uint64_t *pa_owned) {
    if(!pa_owned)return -EINVAL;
    *pa_owned=0;
    if(!file || !file->refs)return -EINVAL;
    return nas_vfs_acquire_shared_page(&file->node,offset,pa_owned);
}
int nas_fs_mmap_dirty(struct nas_mapped_file *file) {
    if(!file || !file->refs)return -EINVAL;
    if(!nas_fs_mmap_writable(file))return -EACCES;
    return nas_vfs_touch_shared(&file->node);
}
int nas_fs_mmap_sync(struct nas_mapped_file *file,bool rearm) {
    if(!file || !file->refs)return -EINVAL;
    return rearm ? nas_vfs_sync_user(&file->node) :
                   nas_vfs_sync(&file->node);
}
bool nas_fs_mmap_matches(struct nas_mapped_file *file,
                         const struct nas_fs_node *node) {
    if(!file || !file->refs || !node)return false;
    const struct nas_fs_node *mine=&file->node;
    return mine->mount==node->mount && mine->mount_id==node->mount_id &&
           mine->filesystem==node->filesystem && mine->ino==node->ino &&
           mine->inode==node->inode;
}
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

static uint64_t io_slot_write(unsigned slot, uint64_t address, uint64_t length) {
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
static uint64_t io_slot_read(unsigned slot, uint64_t address, uint64_t length) {
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
uint64_t nas_fs_fd_write(uint64_t fd, uint64_t address, uint64_t length) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)
        return (uint64_t)-EBADF;
    return io_slot_write(TASK.fds->entries[fd].slot,address,length);
}
uint64_t nas_fs_fd_read(uint64_t fd, uint64_t address, uint64_t length) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)
        return (uint64_t)-EBADF;
    return io_slot_read(TASK.fds->entries[fd].slot,address,length);
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
    /* Linux intentionally differs from POSIX: pwrite() on an O_APPEND
     * description appends to the live inode size without changing the
     * shared OFD seek position. Providers still see explicit write_at. */
    if(opened->status_flags&0x400ULL)
        offset=nas_vfs_node_size(&opened->vnode);
    if(offset>INT64_MAX || count>INT64_MAX-offset)
        return (uint64_t)-EFBIG;
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
struct nas_pending_io {
    struct nas_io_request request;
    uint64_t op,scalar_address,scalar_length;
    bool nonblocking;
};
/* Capture the process-local descriptor mapping once. A pending syscall
 * subsequently holds the shared File by slot, regardless of dup2/close. */
static int io_resolve_fd(uint64_t fd,unsigned *slot) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)
        return -EBADF;
    const unsigned target=TASK.fds->entries[fd].slot;
    if(target>=NAS_OPEN_FILE_LIMIT || !nas_open_files[target].refcount)
        return -EBADF;
    *slot=target;
    return 0;
}
static void io_request_release(struct nas_io_request *req) {
    if(req->vectors)(void)nas_heap_free(req->vectors);
    req->vectors=0;
}
static int io_request_import_slot(unsigned slot,uint64_t user_iov,uint64_t count,
                                  bool writing,struct nas_io_request *req) {
    *req=(struct nas_io_request){0};
    /* Linux looks up and checks the File before importing user iovecs,
     * including an empty iovec array. This also prevents zero-byte writev
     * from silently succeeding on an O_RDONLY/open(O_PATH) descriptor. */
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
static int io_request_import(uint64_t fd,uint64_t user_iov,uint64_t count,
                             bool writing,struct nas_io_request *req) {
    unsigned slot;
    int rc=io_resolve_fd(fd,&slot);
    return rc ? rc : io_request_import_slot(slot,user_iov,count,writing,req);
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
static uint64_t io_segmented_read(const struct nas_io_request *req) {
    uint64_t done=0;
    for(uint64_t i=0;i<req->count && done<NAS_MAX_RW_COUNT;i++) {
        uint64_t bytes=req->vectors[i].length;
        if(!bytes)continue;
        if(bytes>NAS_MAX_RW_COUNT-done)bytes=NAS_MAX_RW_COUNT-done;
        uint64_t got=io_slot_read(req->slot,req->vectors[i].address,bytes);
        if((int64_t)got<0)return done ? done:got;
        done+=got;
        if(got!=bytes || bytes!=req->vectors[i].length)break;
    }
    return done;
}
static uint64_t io_segmented_write(const struct nas_io_request *req) {
    uint64_t done=0;
    for(uint64_t i=0;i<req->count && done<NAS_MAX_RW_COUNT;i++) {
        uint64_t bytes=req->vectors[i].length;
        if(!bytes) {
            /* A leading zero-length segment is handed to a scalar-only
             * provider when later data exists: Linux eventfd.write(0)
             * rejects [0,8], but skips zero entries after progress. */
            if(!done && req->total) {
                uint64_t rc=io_slot_write(req->slot,req->vectors[i].address,0);
                if((int64_t)rc<0)return rc;
            }
            continue;
        }
        if(bytes>NAS_MAX_RW_COUNT-done)bytes=NAS_MAX_RW_COUNT-done;
        uint64_t wrote=io_slot_write(req->slot,req->vectors[i].address,bytes);
        if((int64_t)wrote<0)return done ? done:wrote;
        done+=wrote;
        if(wrote!=bytes || bytes!=req->vectors[i].length)break;
    }
    return done;
}
static uint64_t io_vector_execute(const struct nas_io_request *req,bool writing) {
    if(!req->total)return 0;
    if(writing)
        return req->ops->aggregate_writev ? io_aggregate_write(req) :
                                           io_segmented_write(req);
    return req->ops->aggregate_readv ? io_aggregate_read(req) :
                                      io_segmented_read(req);
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
    uint64_t result=io_vector_execute(&req,false);
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
    uint64_t result=io_vector_execute(&req,true);
    io_request_release(&req);
    return result;
}
/* A File is a shared open-description, not a mutable fd number. This
 * single executor is used by both the first attempt and wake completion. */
static uint64_t io_pending_execute(const struct nas_pending_io *pending) {
    if(pending->op==NAS_SC_read)
        return io_slot_read(pending->request.slot,pending->scalar_address,
                            pending->scalar_length);
    if(pending->op==NAS_SC_write)
        return io_slot_write(pending->request.slot,pending->scalar_address,
                             pending->scalar_length);
    return io_vector_execute(&pending->request,pending->op==NAS_SC_writev);
}
static bool io_read_op(uint64_t op) {
    return op==NAS_SC_read || op==NAS_SC_readv;
}
static bool io_would_block(uint64_t rc) {
    return (int64_t)rc==-EAGAIN || (int64_t)rc==-EOPNOTSUPP;
}
/* Only import metadata once. The stack request owns its File reference
 * during the initial operation; a blocked return transfers BOTH resources
 * to a heap request belonging exclusively to Task.wait. */
void nas_fs_io_start(uint64_t op,uint64_t fd,uint64_t address,uint64_t length,
                     struct nas_io_attempt *attempt) {
    *attempt=(struct nas_io_attempt){.result=(uint64_t)-EBADF};
    if(op!=NAS_SC_read && op!=NAS_SC_readv &&
       op!=NAS_SC_write && op!=NAS_SC_writev) {
        attempt->result=(uint64_t)-EINVAL;
        return;
    }
    unsigned slot;
    int rc=io_resolve_fd(fd,&slot);
    if(rc)return;
    const struct nas_file_ops *ops=nas_open_files[slot].ops;
    attempt->pipe_like=ops && ops->sends_sigpipe;
    if(!nas_fd_hold_description(slot))return;
    struct nas_pending_io current={0};
    current.op=op;
    current.nonblocking=!!(nas_open_files[slot].status_flags&0x800ULL);
    current.scalar_address=address;
    current.scalar_length=length;
    current.request.slot=slot;
    current.request.ops=ops;
    if(op==NAS_SC_readv || op==NAS_SC_writev) {
        rc=io_request_import_slot(slot,address,length,op==NAS_SC_writev,
                                  &current.request);
        if(rc) {
            attempt->result=(uint64_t)(int64_t)rc;
            nas_fs_drop_kernel_description(slot);
            return;
        }
        attempt->tty_requested_bytes=current.request.total;
    } else attempt->tty_requested_bytes=
        length>NAS_MAX_RW_COUNT ? NAS_MAX_RW_COUNT:length;
    attempt->result=io_pending_execute(&current);
    if(nas_fs_io_should_block(&current,attempt->result,true)) {
        struct nas_pending_io *owned=nas_heap_alloc(sizeof(*owned));
        if(!owned)attempt->result=(uint64_t)-ENOMEM;
        else {
            *owned=current;
            attempt->pending=owned;
            return;
        }
    }
    io_request_release(&current.request);
    nas_fs_drop_kernel_description(slot);
}
uint64_t nas_fs_io_resume(struct nas_pending_io *pending) {
    return pending ? io_pending_execute(pending):(uint64_t)-EINVAL;
}
bool nas_fs_io_pipe_like(const struct nas_pending_io *pending) {
    return pending && pending->request.ops &&
           pending->request.ops->sends_sigpipe;
}
bool nas_fs_io_tty_read(const struct nas_pending_io *pending) {
    return pending && io_read_op(pending->op) &&
        pending->request.ops &&
        (pending->request.ops->io_wait_flags&NAS_IO_WAIT_TTY);
}
bool nas_fs_io_should_block(const struct nas_pending_io *pending,
                            uint64_t result,bool first_attempt) {
    if(!pending || pending->nonblocking || !pending->request.ops)return false;
    uint8_t policy=pending->request.ops->io_wait_flags;
    if(nas_fs_io_tty_read(pending))
        return (int64_t)result==-EAGAIN && nas_tty_read_waitable();
    return first_attempt ? (policy&NAS_IO_WAIT_INITIAL) &&
                           (int64_t)result==-EOPNOTSUPP :
                           (policy&NAS_IO_WAIT_RETRY) && io_would_block(result);
}
bool nas_fs_io_ready(const struct nas_pending_io *pending,
                     const struct nas_task *task) {
    if(!pending)return true;
    if(nas_fs_io_tty_read(pending))
        return nas_tty_read_ready((struct nas_task *)task,
                                  task->wait.tty_requested_bytes);
    unsigned slot=pending->request.slot;
    uint16_t events=io_read_op(pending->op)?0x0001U:0x0004U;
    return (nas_fs_poll_description_task(slot,events,task)&
            (events==0x0001U ? 0x0019U:0x001cU))!=0;
}
void nas_fs_io_dispose(struct nas_pending_io *pending) {
    if(!pending)return;
    unsigned slot=pending->request.slot;
    io_request_release(&pending->request);
    (void)nas_heap_free(pending);
    nas_fs_drop_kernel_description(slot);
}
