#include "fd.h"
#include "task.h"
#include "errno.h"
#include "fs/file_ops.h"

/* One global open-description table shared across all task FD tables.
 * Backends are released by the syscall adapter only on final close;
 * the generic description layer has no dependency on /tmp or pipes.
 */
struct nas_open_file nas_open_files[NAS_FD_LIMIT];

void nas_fd_system_init(void) {
    for (unsigned i=0;i<NAS_FD_LIMIT;i++) {
        nas_open_files[i]=(struct nas_open_file){0};
    }
}

/* Called when the owning task's FD entry is closed. Detach exactly one
 * descriptor and tell the caller whether a shared open-description became
 * unreachable; it must then tear down the backend before slot reuse.
 */
unsigned nas_fd_close(struct nas_task *task, unsigned fd) {
    if (!task || fd>=NAS_FD_LIMIT ||
        !task->fds.descriptors[fd]) return NAS_FD_LIMIT;
    const unsigned slot=task->fds.file_slot[fd];
    bool last=false;
    if (nas_open_files[slot].refcount) {
        nas_open_files[slot].refcount--;
        last=nas_open_files[slot].refcount==0;
    }
    task->fds.descriptors[fd]=0;
    task->fds.file_slot[fd]=0;
    task->fds.fd_cloexec[fd]=0;
    return last ? slot : NAS_FD_LIMIT;
}

/* Same open description, independent descriptor-local CLOEXEC bit. */
uint64_t nas_fd_clone(struct nas_task *task, uint64_t src, uint64_t dst,
                      bool cloexec) {
    if (!task || src>=NAS_FD_LIMIT || dst>=NAS_FD_LIMIT ||
        !task->fds.descriptors[src]) return (uint64_t)-EBADF;
    if (src==dst) return dst;
    if (task->fds.descriptors[dst]) return (uint64_t)-EBUSY;
    unsigned slot=task->fds.file_slot[src];
    if (!nas_open_files[slot].refcount) return (uint64_t)-EBADF;
    task->fds.descriptors[dst]=task->fds.descriptors[src];
    task->fds.file_slot[dst]=(uint8_t)slot;
    task->fds.fd_cloexec[dst]=cloexec ? 1 : 0;
    nas_open_files[slot].refcount++;
    return dst;
}
uint64_t nas_fd_duplicate(struct nas_task *task, uint64_t src,
                          uint64_t minimum, bool cloexec) {
    if (!task || src>=NAS_FD_LIMIT || !task->fds.descriptors[src])
        return (uint64_t)-EBADF;
    if (minimum>=NAS_FD_LIMIT) return (uint64_t)-EINVAL;
    for (unsigned fd=(unsigned)minimum;fd<NAS_FD_LIMIT;fd++)
        if (!task->fds.descriptors[fd])
            return nas_fd_clone(task,src,fd,cloexec);
    return (uint64_t)-EMFILE;
}

/* Fork copies each process-local FD entry, but references shared open
 * descriptions. Validate first to avoid a partially incremented table. */
bool nas_task_hold_files(const struct nas_task *task) {
    if (!task) return false;
    for (unsigned fd=0;fd<NAS_FD_LIMIT;fd++)
        if (task->fds.descriptors[fd] &&
            !nas_open_files[task->fds.file_slot[fd]].refcount) return false;
    for (unsigned fd=0;fd<NAS_FD_LIMIT;fd++)
        if (task->fds.descriptors[fd])
            nas_open_files[task->fds.file_slot[fd]].refcount++;
    return true;
}
