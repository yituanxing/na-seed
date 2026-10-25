#include "fd.h"
#include "task.h"
#include "errno.h"
#include "fs/file_ops.h"

/* One global open-description table shared across all task FD tables.
 * Backends are released by the syscall adapter only on final close;
 * the generic description layer has no dependency on /tmp or pipes.
 */
struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];
extern void nas_print(const char *);
extern void nas_dec(uint64_t);

/* Diagnostics are emitted only on a genuine global capacity failure.
 * Separate this from per-process EMFILE and the independent pipe pool. */
void nas_fd_report_exhaustion(const char *operation) {
    unsigned occupied=0;
    for (unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++)
        if (nas_open_files[i].refcount) occupied++;
    nas_print("[FD CAP] operation="); nas_print(operation);
    nas_print(" open="); nas_dec(occupied);
    nas_print(" capacity="); nas_dec(NAS_OPEN_FILE_LIMIT);
    nas_print("\n");
}


void nas_fd_system_init(void) {
    for (unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++) {
        nas_open_files[i]=(struct nas_open_file){0};
    }
}

/* Task-local FD indices are disjoint from global open descriptions.
 * Probing is non-mutating, so failure cannot leak a backend or a partial FD. */
unsigned nas_fd_first_unused(const struct nas_task *task, unsigned minimum) {
    if (!task || !task->fds) return NAS_FD_LIMIT;
    for (unsigned fd=minimum;fd<NAS_FD_LIMIT;fd++)
        if (task->fds->entries[fd].kind==FD_CLOSED) return fd;
    return NAS_FD_LIMIT;
}

int nas_fd_reserve(const struct nas_task *task, unsigned minimum, unsigned count,
                   unsigned fds[2], unsigned slots[2], const char *operation) {
    if (!task || !task->fds || !fds || !slots || !count || count>2)
        return -EINVAL;
    unsigned nfds=0,nslots=0;
    for(unsigned fd=minimum;fd<NAS_FD_LIMIT && nfds<count;fd++)
        if(task->fds->entries[fd].kind==FD_CLOSED) fds[nfds++]=fd;
    if(nfds<count) return -EMFILE;
    for(unsigned slot=0;slot<NAS_OPEN_FILE_LIMIT && nslots<count;slot++)
        if(!nas_open_files[slot].refcount)slots[nslots++]=slot;
    if(nslots<count) {
        if(operation)nas_fd_report_exhaustion(operation);
        return -ENFILE;
    }
    return 0;
}

/* The caller hands off one already-accounted backend description reference.
 * This must NOT increment it: accept() and SCM_RIGHTS transfer ownership. */
bool nas_fd_attach_owned(struct nas_task *task, unsigned fd, unsigned slot,
                         uint8_t kind, bool cloexec) {
    if(!task || !task->fds || fd>=NAS_FD_LIMIT ||
       slot>=NAS_OPEN_FILE_LIMIT || kind==FD_CLOSED ||
       task->fds->entries[fd].kind!=FD_CLOSED ||
       !nas_open_files[slot].refcount) return false;
    task->fds->entries[fd]=(struct nas_fd_entry){
        .kind=kind,.slot=(uint8_t)slot,.cloexec=cloexec ? 1U : 0U
    };
    return true;
}

/* Allocate one shared file description and publish one task-local FD.
 * Backend setup is completed by the caller before exposing the entry. */
bool nas_fd_install_new(struct nas_task *task, unsigned fd, unsigned slot,
                        uint8_t kind, uint64_t status_flags, bool cloexec) {
    if(!task || !task->fds || fd>=NAS_FD_LIMIT ||
       slot>=NAS_OPEN_FILE_LIMIT || kind==FD_CLOSED ||
       task->fds->entries[fd].kind!=FD_CLOSED ||
       nas_open_files[slot].refcount) return false;
    nas_open_files[slot]=(struct nas_open_file){
        .refcount=1,.status_flags=status_flags,
        .ops=nas_fs_ops_for_kind(kind)
    };
    return nas_fd_attach_owned(task,fd,slot,kind,cloexec);
}

/* Called when the owning task's FD entry is closed. Detach exactly one
 * descriptor and tell the caller whether a shared open-description became
 * unreachable; it must then tear down the backend before slot reuse.
 */
unsigned nas_fd_close(struct nas_task *task, unsigned fd) {
    if (!task || fd>=NAS_FD_LIMIT ||
        !task->fds->entries[fd].kind) return NAS_OPEN_FILE_LIMIT;
    const unsigned slot=task->fds->entries[fd].slot;
    if (slot>=NAS_OPEN_FILE_LIMIT) return NAS_OPEN_FILE_LIMIT;
    bool last=false;
    if (nas_open_files[slot].refcount) {
        nas_open_files[slot].refcount--;
        last=nas_open_files[slot].refcount==0;
    }
    task->fds->entries[fd]=(struct nas_fd_entry){0};
    return last ? slot : NAS_OPEN_FILE_LIMIT;
}

/* Same open description, independent descriptor-local CLOEXEC bit. */
uint64_t nas_fd_clone(struct nas_task *task, uint64_t src, uint64_t dst,
                      bool cloexec) {
    if (!task || src>=NAS_FD_LIMIT || dst>=NAS_FD_LIMIT ||
        !task->fds->entries[src].kind) return (uint64_t)-EBADF;
    if (src==dst) return dst;
    if (task->fds->entries[dst].kind) return (uint64_t)-EBUSY;
    unsigned slot=task->fds->entries[src].slot;
    if (slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount) return (uint64_t)-EBADF;
    task->fds->entries[dst]=task->fds->entries[src];
    task->fds->entries[dst].cloexec=cloexec ? 1 : 0;
    nas_open_files[slot].refcount++;
    return dst;
}
uint64_t nas_fd_duplicate(struct nas_task *task, uint64_t src,
                          uint64_t minimum, bool cloexec) {
    if (!task || src>=NAS_FD_LIMIT || !task->fds->entries[src].kind)
        return (uint64_t)-EBADF;
    if (minimum>=NAS_FD_LIMIT) return (uint64_t)-EINVAL;
    for (unsigned fd=(unsigned)minimum;fd<NAS_FD_LIMIT;fd++)
        if (!task->fds->entries[fd].kind)
            return nas_fd_clone(task,src,fd,cloexec);
    return (uint64_t)-EMFILE;
}

/* Fork copies each process-local FD entry, but references shared open
 * descriptions. Validate first to avoid a partially incremented table. */
bool nas_task_hold_files(const struct nas_task *task) {
    if (!task) return false;
    for (unsigned fd=0;fd<NAS_FD_LIMIT;fd++)
        if (task->fds->entries[fd].kind &&
            (task->fds->entries[fd].slot>=NAS_OPEN_FILE_LIMIT ||
              !nas_open_files[task->fds->entries[fd].slot].refcount)) return false;
    for (unsigned fd=0;fd<NAS_FD_LIMIT;fd++)
        if (task->fds->entries[fd].kind)
            nas_open_files[task->fds->entries[fd].slot].refcount++;
    return true;
}
