/* M180: Task-owned Linux ABI and usercopy, worker-owned VFS/lwext4.
 * There is deliberately no arbitrary kernel-preemption path here: a Task
 * explicitly parks at nas_task_block(), and resumes on its ORIGINAL C
 * stack+address space after the worker and block IRQ have completed. */
#include "user_broker.h"
#ifdef NAS_ENABLE_M180_FS_USER_IRQ
#include <stdint.h>
#include <stdbool.h>
#include "../errno.h"
#include "../fd.h"
#include "../task.h"
#include "../task/sched.h"
#include "../mm/heap.h"
#include "../mm/usercopy.h"
#include "../mm/vm.h"
#include "../arch/mmu.h"
#include "../arch/linux_fcntl_abi.h"
#include "../arch/linux_stat_abi.h"
#include "../lib/string.h"
#include "../panic.h"
#include "mode.h"
#include "vfs.h"
#include "lookup.h"
#include "path_request.h"
#include "syscall.h"
#include "file_ops.h"
#include "io.h"
#include "broker.h"

#define M180_TRANSFER_MAX 65536ULL
#define M180_DIRENT_BATCH 8U
enum user_broker_op { UBO_OPEN,UBO_READ,UBO_WRITE,UBO_PREAD,UBO_PWRITE,
                      UBO_READV,UBO_WRITEV,UBO_SYNC,UBO_FSTAT,UBO_STAT,
                      UBO_GETDENTS };
struct user_broker_payload {
    enum user_broker_op op;
    struct nas_fs_path_request path;
    bool path_owned;
    struct nas_vfs_open_options open;
    struct nas_fs_node opened;
    struct nas_vfs_path opened_path;
    bool opened_owned;
    char origin[256];
    struct nas_linux_stat st;
    unsigned fd,slot;
    bool description_held;
    struct nas_fd_slot_lease lease;
    bool slot_leased;
    struct nas_files *files_hold;
    bool fd_reserved;
    uint64_t flags,user_address,length,offset;
    uint8_t *buffer;
    struct nas_fs_io_vectors vectors; /* Task-owned, never usercopy in Worker */
    unsigned dirent_count;
    struct nas_vfs_dirent dirents[M180_DIRENT_BATCH];
};

static bool broker_file(unsigned fd,unsigned *slot) {
    struct nas_task *task=nas_current_task;
    if(!task || !task->fds || fd>=NAS_FD_LIMIT ||
       task->fds->entries[fd].kind==FD_CLOSED)return false;
    unsigned found=task->fds->entries[fd].slot;
    if(found>=NAS_OPEN_FILE_LIMIT)return false;
    const struct nas_open_file *file=&nas_open_files[found];
    if(!file->refcount || !file->vnode_pinned ||
       !file->vnode.mount || !file->vnode.mount->worker_io)return false;
    if(slot)*slot=found;
    return true;
}

static bool valid_user_range(uint64_t addr,uint64_t n) {
    return addr<USER_LIMIT && n<=USER_LIMIT-addr;
}

static void broker_stat_from_inode(struct nas_linux_stat *st,
                                   const struct nas_fs_node *inode,
                                   uint64_t device) {
    *st=(struct nas_linux_stat){0};
    st->dev=device;st->ino=inode->ino;st->rdev=inode->rdev;
    st->mode=inode->mode;st->nlink=inode->nlink;
    st->uid=inode->uid;st->gid=inode->gid;
    st->size=(int64_t)inode->size;
    st->blocks=(int64_t)((inode->size+511U)/512U);
    st->blksize=PAGE;
    st->atime_sec=(int64_t)inode->atime_sec;
    st->mtime_sec=(int64_t)inode->mtime_sec;
    st->ctime_sec=(int64_t)inode->ctime_sec;
    st->atime_nsec=inode->atime_nsec;
    st->mtime_nsec=inode->mtime_nsec;
    st->ctime_nsec=inode->ctime_nsec;
}

static void broker_path_drop(struct user_broker_payload *p) {
    if(p->path_owned) {
        nas_fs_path_end(&p->path);
        p->path_owned=false;
    }
}
static void broker_open_drop(struct user_broker_payload *p) {
    if(!p->opened_owned)return;
    if(p->opened_path.dentry && nas_vfs_path_put(&p->opened_path))
        nas_fail("M180 abandoned open Dentry corruption");
    if(nas_vfs_node_unpin(&p->opened))
        nas_fail("M180 abandoned open vnode corruption");
    p->opened_owned=false;
}
/* Executed only on FS worker (also for detached Task's open result). */
static void broker_orphan_cleanup(void *payload) {
    struct user_broker_payload *p=payload;
    broker_open_drop(p);
    broker_path_drop(p);
    if(p->description_held) {
        p->description_held=false;
        nas_fs_drop_kernel_description(p->slot);
    }
}
static int64_t broker_worker_op(void *payload) {
    struct user_broker_payload *p=payload;
    int64_t answer=-EINVAL;
    if(p->op==UBO_OPEN) {
        int rc=nas_vfs_open_path_at(p->path.start.dentry?&p->path.start:NULL,
              p->path.root.dentry?&p->path.root:NULL,p->path.pathname,
              &p->open,&p->opened,&p->opened_path);
        if(!rc) {
            p->opened_owned=true;
            /* The Linux descriptor class registry is Task-owned. Ordinary
             * character/TTY files stay on their pre-existing sync path. */
            if((p->opened.mode&0170000U)==0020000U) {
                broker_open_drop(p);
                rc=-EOPNOTSUPP;
            }
        }
        answer=rc;
    } else if(p->op==UBO_STAT) {
        struct nas_vfs_path found={0};
        struct nas_fs_node inode={0},fresh={0};
        int rc=nas_fs_lookup_path_at(
            p->path.start.dentry?&p->path.start:NULL,
            p->path.root.dentry?&p->path.root:NULL,
            p->path.pathname,!(p->flags&0x100U),&found);
        if(!rc)rc=nas_vfs_path_node(&found,&inode);
        if(!rc)rc=nas_vfs_getattr(&inode,&fresh);
        if(!rc) {
            uint64_t dev=nas_vfs_node_device(&fresh);
            if(!dev)rc=-ENODEV;
            else broker_stat_from_inode(&p->st,&fresh,dev);
        }
        if(found.dentry && nas_vfs_path_put(&found))
            nas_fail("M180 stat path ownership corrupted");
        answer=rc;
    } else {
        struct nas_open_file *file=&nas_open_files[p->slot];
        const struct nas_fs_node *node=&file->vnode;
        if(!file->refcount || !file->vnode_pinned ||
           !file->vnode.mount || !file->vnode.mount->worker_io)
            answer=-EBADF;
        else if(p->op==UBO_READ || p->op==UBO_PREAD ||
               p->op==UBO_READV) {
            uint64_t where=p->op==UBO_PREAD?p->offset:file->position;
            if(node->is_dir)answer=-EISDIR;
            else if(!p->length)answer=0;
            else answer=nas_vfs_read_at(node,where,p->buffer,p->length);
            if((p->op==UBO_READ || p->op==UBO_READV) && answer>0)
                file->position+=answer;
        } else if(p->op==UBO_WRITE || p->op==UBO_PWRITE ||
                  p->op==UBO_WRITEV) {
            uint64_t where=p->op==UBO_PWRITE?p->offset:file->position;
            if(file->status_flags&0x400ULL)
                where=nas_vfs_node_size(node);
            if(node->is_dir)answer=-EISDIR;
            else if(!p->length)answer=nas_vfs_can_write(node)?0:-EROFS;
            else answer=nas_vfs_write_at(node,where,p->buffer,p->length);
            if(answer>0) {
                if(where+(uint64_t)answer>file->vnode.size)
                    file->vnode.size=where+(uint64_t)answer;
                if(p->op==UBO_WRITE || p->op==UBO_WRITEV)
                    file->position=where+(uint64_t)answer;
            }
        } else if(p->op==UBO_SYNC) {
            p->opened=*node; /* borrowed snapshot; own held File protects it */
            answer=nas_vfs_sync(node);
        }
        else if(p->op==UBO_FSTAT) {
            struct nas_fs_node fresh={0};
            int rc=nas_vfs_getattr(node,&fresh);
            if(!rc) {
                uint64_t dev=nas_vfs_node_device(&fresh);
                if(!dev)rc=-ENODEV;
                else broker_stat_from_inode(&p->st,&fresh,dev);
            }
            answer=rc;
        } else if(p->op==UBO_GETDENTS) {
            if(!node->is_dir)answer=-ENOTDIR;
            else {
                int count=nas_vfs_read_dir(node,file->position,p->dirents,
                                           M180_DIRENT_BATCH);
                if(count>=0)p->dirent_count=(unsigned)count;
                answer=count;
            }
        }
        /* Keep File pinned until original Task consumes results. Detach
         * subsequently requeues orphan cleanup on THIS worker, so a last
         * close cannot recycle slot or block in another Task's C frame. */
    }
    broker_path_drop(p);
    return answer;
}

static void broker_payload_release(void *payload) {
    struct user_broker_payload *p=payload;
    /* No VFS release here: this callback may run in a Task/exit context.
     * OPEN's unclaimed VFS leases are handled by broker_orphan_cleanup on
     * the filesystem worker, and submitted path refs by broker_worker_op. */
    if(p->opened_owned)
        nas_fail("M180 unclaimed inode escaped worker abandonment cleanup");
    if(p->description_held)
        nas_fail("M180 pinned File escaped worker completion");
    if(p->path_owned)broker_path_drop(p); /* pre-submit failed request only */
    if(p->slot_leased && !nas_fd_slot_lease_cancel(p->lease))
        nas_fail("M180 File lease cancel failed");
    if(p->fd_reserved && !nas_fd_pending_cancel(p->files_hold,p->fd))
        nas_fail("M180 FD reservation cancel failed");
    if(p->buffer && !nas_heap_free(p->buffer))
        nas_fail("M180 syscall buffer release failed");
    nas_fs_io_vectors_release(&p->vectors);
}

static uint64_t broker_finish(struct nas_task *task) {
    struct nas_fs_broker_request *request=task->wait.fs_request;
    if(!request || !nas_fs_broker_request_ready(request))
        nas_fail("M180 completed nonready filesystem syscall");
    struct user_broker_payload *p=nas_fs_broker_request_payload(request);
    if(!p)nas_fail("M180 lost Task-owned filesystem payload");
    int64_t rc=nas_fs_broker_request_result(request);
    if(rc==0 && p->op==UBO_OPEN) {
        const uint8_t kind=p->opened.is_dir?FD_DIR:FD_ARCHIVE;
        if(!nas_fd_pending_valid(p->files_hold,p->fd))rc=-EBUSY;
        else {
            /* On this one nonpreemptible Task C frame, clear the private
             * unpublished FD token immediately before lease publication.
             * No sibling can schedule between these two operations. */
            if(!nas_fd_pending_commit(p->files_hold,p->fd))
                nas_fail("M180 pending FD failed atomic handoff");
            p->fd_reserved=false;p->files_hold=NULL;
            rc=nas_fd_slot_lease_commit(p->lease,task,p->fd,kind,
                    nas_linux_open_status_flags(p->flags),
                    (p->flags&0x80000ULL)!=0,NULL);
        }
        if(!rc) {
            p->slot_leased=false;
            struct nas_open_file *file=&nas_open_files[p->lease.slot];
            file->vnode=p->opened;file->vnode_pinned=true;
            file->vpath=p->opened_path;
            /* Diagnostic only; never reopen stale path by this spelling. */
            memcpy(file->origin_path,p->origin,sizeof(p->origin));
            p->opened_owned=false;
            if(!nas_fs_broker_request_claim(request))
                nas_fail("M180 published FD without request ownership claim");
            rc=(int64_t)p->fd;
        }
    } else if(rc>=0 && (p->op==UBO_READ || p->op==UBO_PREAD)) {
        if(rc && !nas_user_write_bytes(p->user_address,p->buffer,(size_t)rc))
            rc=-EFAULT;
    } else if(rc>=0 && p->op==UBO_READV && rc) {
        uint64_t copied=0;
        if(!nas_fs_io_vectors_copyout(&p->vectors,p->buffer,(uint64_t)rc,
                                       &copied))
            rc=copied?(int64_t)copied:-EFAULT;
    } else if(rc>=0 && (p->op==UBO_STAT || p->op==UBO_FSTAT)) {
        if(!nas_user_write_bytes(p->user_address,(const uint8_t *)&p->st,
                                sizeof(p->st)))rc=-EFAULT;
    } else if(rc>=0 && p->op==UBO_SYNC) {
        /* VFS dirty-PTE rearm is Task/MM work, never worker work. */
        nas_vm_rearm_file(&p->opened);
    } else if(rc>=0 && p->op==UBO_GETDENTS) {
        struct nas_open_file *file=&nas_open_files[p->slot];
        uint64_t wrote=0;
        uint64_t cursor=file->position;
        for(unsigned i=0;i<p->dirent_count;i++) {
            const struct nas_vfs_dirent *e=&p->dirents[i];
            unsigned length=e->name_len;
            if(!e->ino || !length || length>=256 || e->name[length]) {
                rc=wrote?(int64_t)wrote:-EIO;break;
            }
            unsigned bytes=(19+length+1+7U)&~7U;
            if(bytes>p->length-wrote) {
                rc=wrote?(int64_t)wrote:-EINVAL;break;
            }
            uint8_t raw[280]={0};
            for(unsigned b=0;b<8;b++) {
                raw[b]=(uint8_t)(e->ino>>(8*b));
                raw[b+8]=(uint8_t)((cursor+1)>>(8*b));
            }
            raw[16]=(uint8_t)bytes;raw[17]=(uint8_t)(bytes>>8);
            raw[18]=e->dtype;memcpy(raw+19,e->name,length);
            if(!nas_user_write_bytes(p->user_address+wrote,raw,bytes)) {
                rc=wrote?(int64_t)wrote:-EFAULT;break;
            }
            wrote+=bytes;cursor++;
            rc=(int64_t)wrote;
        }
        file->position=cursor;
        if(!p->dirent_count)rc=0;
    }
    nas_fs_broker_note_task_woken();
    return (uint64_t)rc;
}

static struct nas_fs_user_broker_answer handled(uint64_t value) {
    return (struct nas_fs_user_broker_answer){true,value};
}
static struct nas_fs_user_broker_answer not_handled(void) {
    return (struct nas_fs_user_broker_answer){false,0};
}

/* A read-only namei before submitting an OPEN/STAT is sufficient to decide
 * whether this path can hit worker_io. It never creates/truncates anything.
 * An -EBUSY at any symlink/dirfd/mount transition means the worker owns the
 * continuation; no pathname prefix heuristic or second mount namespace. */
static bool path_needs_worker(struct nas_fs_path_request *path,
                              bool create,bool follow_final) {
    struct nas_vfs_path found={0};
    int rc=nas_fs_lookup_path_at(path->start.dentry?&path->start:NULL,
        path->root.dentry?&path->root:NULL,
        path->pathname,follow_final,&found);
    if(found.dentry && nas_vfs_path_put(&found))
        nas_fail("M180 speculative namei dentry corruption");
    if(rc==-EBUSY)return true;
    if(rc!=-ENOENT || !create)return false;
    struct nas_vfs_path parent={0};char leaf[256];
    rc=nas_fs_resolve_create_parent_at(
        path->start.dentry?&path->start:NULL,
        path->root.dentry?&path->root:NULL,
        path->pathname,&parent,leaf);
    if(parent.dentry && nas_vfs_path_put(&parent))
        nas_fail("M180 speculative parent dentry corruption");
    return rc==-EBUSY;
}

struct nas_fs_user_broker_answer nas_fs_user_broker_enter(
    uint64_t *frame,enum nas_syscall_op nr,const uint64_t *args) {
    struct nas_task *task=nas_current_task;
    if(!frame || !task || !args)return not_handled();
    enum user_broker_op op=UBO_OPEN;
    bool path_op=false;
    unsigned slot=NAS_OPEN_FILE_LIMIT;
    uint64_t flags=0,dirfd=(uint64_t)-100,pathaddr=0;
    if(nr==NAS_SC_open || nr==NAS_SC_openat) {
        path_op=true;op=UBO_OPEN;
        dirfd=nr==NAS_SC_open ? (uint64_t)-100:args[1];
        pathaddr=nr==NAS_SC_open ? args[1]:args[2];
        if(!nas_linux_open_flags(nr==NAS_SC_open?args[2]:args[3],&flags))
            return not_handled();
        flags=nas_linux_path_only_flags(flags);
        if(flags&~(NAS_LINUX_O_PATH|3ULL|0x800ULL|0x1000ULL|0x8000ULL|
                 0x10000ULL|0x20000ULL|0x80000ULL|0x100ULL|0x80ULL|
                 0x40ULL|0x200ULL|0x400ULL))return not_handled();
    } else if(nr==NAS_SC_stat || nr==NAS_SC_lstat ||
              nr==NAS_SC_newfstatat) {
        path_op=true;op=UBO_STAT;
        dirfd=nr==NAS_SC_newfstatat?args[1]:(uint64_t)-100;
        pathaddr=nr==NAS_SC_newfstatat?args[2]:args[1];
        flags=nr==NAS_SC_newfstatat?args[4]:
              (nr==NAS_SC_lstat?0x100ULL:0);
        if(flags&~(0x100ULL|0x800ULL))return not_handled();
    } else if(nr==NAS_SC_read || nr==NAS_SC_write ||
              nr==NAS_SC_readv || nr==NAS_SC_writev ||
              nr==NAS_SC_pread64 || nr==NAS_SC_pwrite64 ||
              nr==NAS_SC_fsync || nr==NAS_SC_fdatasync ||
              nr==NAS_SC_fstat || nr==NAS_SC_getdents64) {
        if(args[1]>=NAS_FD_LIMIT ||
           !broker_file((unsigned)args[1],&slot))return not_handled();
        if(nr==NAS_SC_read)op=UBO_READ;
        else if(nr==NAS_SC_write)op=UBO_WRITE;
        else if(nr==NAS_SC_readv)op=UBO_READV;
        else if(nr==NAS_SC_writev)op=UBO_WRITEV;
        else if(nr==NAS_SC_pread64)op=UBO_PREAD;
        else if(nr==NAS_SC_pwrite64)op=UBO_PWRITE;
        else if(nr==NAS_SC_fsync || nr==NAS_SC_fdatasync)op=UBO_SYNC;
        else if(nr==NAS_SC_fstat)op=UBO_FSTAT;
        else op=UBO_GETDENTS;
    } else return not_handled();

    struct nas_fs_broker_request *request=nas_fs_broker_request_new(
        sizeof(struct user_broker_payload),broker_worker_op,
        broker_payload_release);
    if(!request)return handled((uint64_t)-ENOMEM);
    struct user_broker_payload *p=nas_fs_broker_request_payload(request);
    if(!p)nas_fail("M180 newly allocated payload missing");
    p->op=op;p->slot=slot;p->fd=NAS_FD_LIMIT;
    p->lease.slot=NAS_OPEN_FILE_LIMIT;
    p->flags=flags;
    int rc=0;
    if(path_op) {
        rc=nas_fs_user_path_begin(dirfd,pathaddr,&p->path);
        if(rc) {
            nas_fs_broker_request_detach(request);
            return not_handled(); /* original decoder preserves its errno */
        }
        p->path_owned=true;
        if(op==UBO_OPEN) {
            size_t n=strnlen(p->path.pathname,sizeof(p->origin)-1);
            memcpy(p->origin,p->path.pathname,n);
            p->origin[n]=0;
        }
        bool needed=path_needs_worker(&p->path,
            op==UBO_OPEN && (flags&0x40ULL),
            op==UBO_STAT ? !(flags&0x100ULL) :
            (!(flags&0x20000ULL) &&
             !((flags&0x40ULL)&&(flags&0x80ULL))));
        if(!needed) {
            nas_fs_broker_request_detach(request);
            return not_handled();
        }
        if(op==UBO_OPEN) {
            p->open=(struct nas_vfs_open_options){
                .follow_final=(flags&0x20000ULL)==0,
                .create=(flags&0x40ULL)!=0,
                .exclusive=(flags&0x80ULL)!=0,
                .truncate=(flags&0x200ULL)!=0,
                .path_only=(flags&NAS_LINUX_O_PATH)!=0,
                .directory_only=(flags&0x10000ULL)!=0,
                .access=(unsigned)(flags&3ULL),
                .create_mode=nas_fs_create_mode(
                    nr==NAS_SC_open?args[3]:args[4],task->fs->umask)
            };
            rc=nas_fd_pending_begin(task,&p->fd,&p->files_hold);
            if(!rc) {
                p->fd_reserved=true;
                rc=nas_fd_slot_lease_begin(&p->lease,"m180-openat");
                if(!rc)p->slot_leased=true;
            }
        } else p->user_address=nr==NAS_SC_newfstatat?args[3]:args[2];
    } else {
        p->user_address=args[2];
        p->length=args[3];
        p->offset=args[4];
        struct nas_open_file *file=&nas_open_files[slot];
        if(file->status_flags&NAS_LINUX_O_PATH)rc=-EBADF;
        if(!rc && (op==UBO_READ || op==UBO_PREAD || op==UBO_READV) &&
           (file->status_flags&3ULL)==1ULL)rc=-EBADF;
        if(!rc && (op==UBO_WRITE || op==UBO_PWRITE || op==UBO_WRITEV) &&
           (file->status_flags&3ULL)==0ULL)rc=-EBADF;
        if(!rc && (op==UBO_PREAD || op==UBO_PWRITE) &&
           p->offset>INT64_MAX)rc=-EINVAL;
        if(!rc && (op==UBO_PREAD || op==UBO_PWRITE) &&
           p->length>INT64_MAX-p->offset)rc=-EINVAL;
        if(!rc && (op==UBO_READV || op==UBO_WRITEV)) {
            /* Import the complete iovec metadata with exactly the same
             * Linux IOV_MAX/address/access rules used by normal File I/O.
             * A single Task-side kernel buffer is the only data the
             * IRQ-backed filesystem worker ever sees. */
            const bool writing=op==UBO_WRITEV;
            rc=nas_fs_io_vectors_import(slot,p->user_address,p->length,
                                         writing,&p->vectors);
            if(!rc) {
                p->length=p->vectors.total;
                if(p->length>M180_TRANSFER_MAX)p->length=M180_TRANSFER_MAX;
                if(p->length) {
                    uint64_t accessible=
                        nas_fs_io_vectors_accessible_prefix(
                            &p->vectors,p->length,writing);
                    if(!accessible)rc=-EFAULT;
                    else p->length=accessible;
                }
            }
            if(!rc && p->length) {
                p->buffer=nas_heap_alloc((size_t)p->length);
                if(!p->buffer)rc=-ENOMEM;
            }
            if(!rc && writing && p->length &&
               !nas_fs_io_vectors_copyin(&p->vectors,p->buffer,p->length))
                rc=-EFAULT;
        } else if(!rc && (op==UBO_READ || op==UBO_WRITE ||
                          op==UBO_PREAD || op==UBO_PWRITE)) {
            if(p->length>M180_TRANSFER_MAX)p->length=M180_TRANSFER_MAX;
            if(!valid_user_range(p->user_address,p->length))rc=-EFAULT;
            if(!rc && p->length) {
                p->buffer=nas_heap_alloc((size_t)p->length);
                if(!p->buffer)rc=-ENOMEM;
            }
            if(!rc && (op==UBO_WRITE || op==UBO_PWRITE) && p->length &&
               !nas_user_read_bytes(p->user_address,p->buffer,
                                    (size_t)p->length))rc=-EFAULT;
            if(!rc && (op==UBO_READ || op==UBO_PREAD) && p->length &&
               !nas_user_prepare_write(p->user_address,p->length))rc=-EFAULT;
        } else if(!rc && op==UBO_FSTAT)
            p->user_address=args[2];
        else if(!rc && op==UBO_GETDENTS) {
            if(p->length<24)rc=-EINVAL;
            if(!rc && !valid_user_range(p->user_address,p->length))rc=-EFAULT;
            if(!rc && !nas_user_prepare_write(p->user_address,
                     p->length<280?p->length:280))rc=-EFAULT;
        }
        if(!rc && !nas_fd_hold_description(slot))rc=-ENFILE;
        if(!rc)p->description_held=true;
    }
    if(rc) {
        /* The request never ran on a worker. No competing native Task
         * executed between this hold and local rejection. */
        if(p->description_held) {
            p->description_held=false;
            nas_fs_drop_kernel_description(p->slot);
        }
        nas_fs_broker_request_detach(request);
        return handled((uint64_t)(int64_t)rc);
    }
    if(!nas_fs_broker_request_set_abandon(request,broker_orphan_cleanup))
        nas_fail("M180 failed to arm deferred VFS cleanup");
    rc=nas_fs_broker_request_submit(request);
    if(rc) {
        if(p->description_held) {
            p->description_held=false;
            nas_fs_drop_kernel_description(p->slot);
        }
        nas_fs_broker_request_detach(request);
        return handled((uint64_t)(int64_t)rc);
    }
    task->wait.fs_request=request;
    nas_fs_broker_note_task_parked();
    uint64_t result=nas_task_block(frame,nr,args[1],args[2],args[3],args[4],
                                   broker_finish);
    return handled(result);
}
#endif
