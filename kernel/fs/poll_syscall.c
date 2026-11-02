/* Kernel-owned poll interests and shared Linux poll/select ABI. */
#include <stdint.h>
#include <stdbool.h>
#include <limine.h>
#include "../errno.h"
#include "../task.h"
#include "../syscall/ops.h"
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
#include "syscall.h"
#include "../pipe.h"
#include "../time/clock.h"
#include "../drivers/tty.h"
#include "../drivers/pty.h"
#include "file_ops.h"
#include "../task/wait.h"
#include "unix_socket.h"
#define TASK (*nas_current_task)

/* One parkable poll/select request owns an immutable interest snapshot and
 * one checked File ref per watched FD. It is created only when the initial
 * attempt actually needs to sleep; no parallel FD map is maintained. */
struct nas_poll_watch {
    uint64_t original; /* original pollfd integer + requested events */
    uint16_t events;
    uint8_t slot;
    uint8_t kind;
    bool valid,bad_fd,pinned;
};
struct nas_poll_snapshot {
    unsigned count;
    bool selected;
    uint64_t readset,writeset,exceptset;
    struct nas_poll_watch watches[NAS_FD_LIMIT];
};
void nas_fs_poll_snapshot_release(struct nas_poll_snapshot *snap) {
    if(!snap)return;
    for(unsigned i=0;i<snap->count;i++)
        if(snap->watches[i].pinned)
            nas_fs_drop_kernel_description(snap->watches[i].slot);
    (void)nas_heap_free(snap);
}
static int poll_import(struct nas_poll_snapshot *snap,uint64_t address,
                       uint64_t nfds,bool pin) {
    *snap=(struct nas_poll_snapshot){0};
    if(nfds>NAS_FD_LIMIT)return -EINVAL;
    snap->count=(unsigned)nfds;
    if(nfds && (address>=USER_LIMIT || nfds>(USER_LIMIT-address)/8))
        return -EFAULT;
    for(unsigned i=0;i<nfds;i++) {
        uint64_t word;
        if(!nas_user_read_u64(address+i*8,&word))return -EFAULT;
        struct nas_poll_watch *w=&snap->watches[i];
        w->original=word&0x0000ffffffffffffULL;
        w->events=(uint16_t)(word>>32);
        int32_t fd=(int32_t)(uint32_t)word;
        if(fd<0)continue;
        if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED) {
            w->bad_fd=true;continue;
        }
        unsigned slot=TASK.fds->entries[fd].slot;
        if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount ||
           nas_open_files[slot].lease_token) {
            w->bad_fd=true;continue;
        }
        w->slot=(uint8_t)slot;
        w->kind=TASK.fds->entries[fd].kind;
        w->valid=true;
        if(pin) {
            if(!nas_fd_hold_description(slot))return -ENFILE;
            w->pinned=true;
        }
    }
    return 0;
}
static bool select_read_bits(uint64_t ptr,uint64_t *mask) {
    *mask=0;
    return !ptr || nas_user_read_u64(ptr,mask);
}
static int select_import(struct nas_poll_snapshot *snap,uint64_t nfds,
                         uint64_t readfds,uint64_t writefds,
                         uint64_t exceptfds,bool pin) {
    *snap=(struct nas_poll_snapshot){.selected=true};
    if(nfds>NAS_FD_LIMIT)return -EINVAL;
    snap->count=(unsigned)nfds;
    if(!select_read_bits(readfds,&snap->readset) ||
       !select_read_bits(writefds,&snap->writeset) ||
       !select_read_bits(exceptfds,&snap->exceptset))return -EFAULT;
    const uint64_t limit=nfds==64?UINT64_MAX:((1ULL<<nfds)-1);
    snap->readset&=limit;snap->writeset&=limit;snap->exceptset&=limit;
    for(unsigned fd=0;fd<nfds;fd++) {
        uint64_t bit=1ULL<<fd;
        struct nas_poll_watch *w=&snap->watches[fd];
        if(snap->readset&bit)w->events|=1U;
        if(snap->writeset&bit)w->events|=4U;
        if(snap->exceptset&bit)w->events|=2U;
        if(!w->events)continue;
        if(TASK.fds->entries[fd].kind==FD_CLOSED)return -EBADF;
        unsigned slot=TASK.fds->entries[fd].slot;
        if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount ||
           nas_open_files[slot].lease_token)return -EBADF;
        w->slot=(uint8_t)slot;
        w->kind=TASK.fds->entries[fd].kind;
        w->valid=true;
        if(pin) {
            if(!nas_fd_hold_description(slot))return -ENFILE;
            w->pinned=true;
        }
    }
    return 0;
}
static bool poll_snapshot_ready(const struct nas_poll_snapshot *snap,
                                 const struct nas_task *task) {
    for(unsigned i=0;i<snap->count;i++) {
        const struct nas_poll_watch *w=&snap->watches[i];
        if(w->bad_fd)return true;
        if(w->valid && nas_fs_poll_description_task(w->slot,w->events,task))
            return true;
    }
    return false;
}

/* Kernel Linux pollfd ABI: int32 fd, int16 events, int16 revents.
 * The only supported blocking timeout is infinite; positive timeouts need
 * monotonic deadline ownership is provided by the Task scheduler.
 * Readiness and the snapshot are distinct: pollfd copy-out is in the
 * owning task's VM, scheduler tests only a kernel-owned interest snapshot.
 */
static uint64_t poll_execute(const struct nas_poll_snapshot *snap,
                             const struct nas_task *task,uint64_t address,
                             uint64_t timeout) {
    uint64_t ready=0;
    uint16_t revents[NAS_FD_LIMIT]={0};
    for(unsigned i=0;i<snap->count;i++) {
        const struct nas_poll_watch *w=&snap->watches[i];
        revents[i]=w->bad_fd ? 0x0020U :
            w->valid ? nas_fs_poll_description_task(w->slot,w->events,task):0;
        if(revents[i])ready++;
    }
    /* A read-only output buffer is EFAULT even when nothing is ready.
     * Never park a Task on a destination that cannot be written. */
    if(snap->count && !nas_user_prepare_write(address,snap->count*8U))
        return (uint64_t)-EFAULT;
    int32_t ms=(int32_t)(uint32_t)timeout;
    if(!ready && ms!=0 &&
       !(task->wait.deadline_active &&
         nas_time_deadline_expired(nas_arch_monotonic_ns(),
                                   task->wait.deadline_ns)))
        return (uint64_t)-EAGAIN;
    /* All output pages were preflighted before modifying the first fd. */
    for(unsigned i=0;i<snap->count;i++) {
        uint64_t word=snap->watches[i].original|
                      ((uint64_t)revents[i]<<48);
        if(!nas_user_write_u64(address+i*8,word))return (uint64_t)-EFAULT;
    }
    return ready;
}
uint64_t nas_fs_poll(uint64_t address, uint64_t nfds, uint64_t timeout) {
    struct nas_poll_snapshot snap;
    int rc=poll_import(&snap,address,nfds,false);
    if(rc)return (uint64_t)(int64_t)rc;
    return poll_execute(&snap,nas_current_task,address,timeout);
}
uint64_t nas_fs_poll_resume(const struct nas_task *task) {
    if(!task || !task->wait.poll_snapshot ||
       task->wait.poll_snapshot->selected)return (uint64_t)-EINVAL;
    return poll_execute(task->wait.poll_snapshot,task,
                        task->wait.blocked_args[0],
                        task->wait.blocked_op==NAS_SC_poll ?
                        task->wait.blocked_args[2]:(uint64_t)-1);
}
/* Linux asm-generic supplies ppoll(73), not legacy poll(7).
 * A NULL timeout means infinity, zero timespec means immediate. A finite
 * timeout is converted once by the dispatcher to an absolute Task deadline.
 * Atomic temporary sigmask remains unsupported until signal frames exist. */
uint64_t nas_fs_ppoll(uint64_t addr,uint64_t nfds,
                      uint64_t timeout_ptr,uint64_t sigmask_ptr) {
    /* Temporary sigmask belongs to dispatcher/scheduler, not FS I/O. */
    (void)sigmask_ptr;
    if(!timeout_ptr)return nas_fs_poll(addr,nfds,(uint64_t)-1);
    uint64_t secs,nsecs;
    if(!nas_user_read_u64(timeout_ptr,&secs) ||
       timeout_ptr>UINT64_MAX-8 ||
       !nas_user_read_u64(timeout_ptr+8,&nsecs))
        return (uint64_t)-EFAULT;
    uint64_t ns;
    if(!nas_time_duration_ns(secs,nsecs,&ns))return (uint64_t)-EINVAL;
    return nas_fs_poll(addr,nfds,ns ? (uint64_t)-1 : 0);
}
/* Native Linux pselect6 over the EXISTING file-ops poll providers.
 * Like ppoll, only the owning Task touches userspace. No foreign address
 * space is consulted while another task's pipe/jobserver wakes up.
 * fd_set is an array of native unsigned-long words; our bounded 64-FD
 * implementation validates and transfers exactly one 64-bit word.
 * Input interests are preserved on EAGAIN for syscall-boundary replay. */
static uint64_t select_execute(const struct nas_poll_snapshot *snap,
                               const struct nas_task *task,
                               uint64_t readfds,uint64_t writefds,
                               uint64_t exceptfds,bool immediate) {
    uint64_t readready=0,writeready=0,exceptready=0;
    uint64_t count=0;
    for(unsigned fd=0;fd<snap->count;fd++) {
        uint64_t bit=1ULL<<fd;
        const struct nas_poll_watch *w=&snap->watches[fd];
        if(!w->valid)continue;
        uint16_t revents=nas_fs_poll_description_task(w->slot,w->events,task);
        bool any=false;
        if((snap->readset&bit) && (revents&(1U|8U|16U))) {
            readready|=bit;any=true;
        }
        if((snap->writeset&bit) && (revents&(4U|8U|16U))) {
            writeready|=bit;any=true;
        }
        if((snap->exceptset&bit) && (revents&2U)) {
            exceptready|=bit;any=true;
        }
        if(any)count++;
    }
    if((readfds && !nas_user_prepare_write(readfds,8)) ||
       (writefds && !nas_user_prepare_write(writefds,8)) ||
       (exceptfds && !nas_user_prepare_write(exceptfds,8)))
        return (uint64_t)-EFAULT;
    if(!count && !immediate &&
       !(task->wait.deadline_active &&
         nas_time_deadline_expired(nas_arch_monotonic_ns(),task->wait.deadline_ns)))
        return (uint64_t)-EAGAIN;
    if((readfds && !nas_user_write_u64(readfds,readready)) ||
       (writefds && !nas_user_write_u64(writefds,writeready)) ||
       (exceptfds && !nas_user_write_u64(exceptfds,exceptready)))
        return (uint64_t)-EFAULT;
    return count;
}
uint64_t nas_fs_pselect6(uint64_t nfds,uint64_t readfds,
                         uint64_t writefds,uint64_t exceptfds,
                         bool immediate) {
    struct nas_poll_snapshot snap;
    int rc=select_import(&snap,nfds,readfds,writefds,exceptfds,false);
    if(rc)return (uint64_t)(int64_t)rc;
    return select_execute(&snap,nas_current_task,readfds,writefds,
                          exceptfds,immediate);
}
uint64_t nas_fs_pselect_resume(const struct nas_task *task) {
    if(!task || !task->wait.poll_snapshot ||
       !task->wait.poll_snapshot->selected)return (uint64_t)-EINVAL;
    const uint64_t *a=task->wait.blocked_args;
    return select_execute(task->wait.poll_snapshot,task,a[1],a[2],a[3],false);
}
/* Staging is transactional: a failed late hold drops every earlier File
 * pin. Task.wait owns the final snapshot through completion, EINTR and exit. */
static int snapshot_arm(struct nas_poll_snapshot *snap) {
    struct nas_task *task=nas_current_task;
    if(task->wait.poll_snapshot) {
        nas_fs_poll_snapshot_release(snap);
        return -EBUSY;
    }
    nas_wait_io_clear(task);
    for(unsigned fd=0;fd<snap->count;fd++) {
        const struct nas_poll_watch *w=&snap->watches[fd];
        if(!w->valid)continue;
        uint16_t interest=0;
        if(w->events&(0x0001U|0x0002U|0x2000U))interest|=1U;
        if(w->events&0x0004U)interest|=4U;
        /* POLLERR/HUP are observable even with events=0. */
        if(!interest)interest=5U;
        nas_wait_io_arm(task,w->slot,interest,
                        (w->events&(0x0001U|0x0002U)) &&
                        (w->kind==FD_TTY || w->kind==FD_UART) &&
                        nas_tty_read_waitable());
    }
    task->wait.poll_snapshot=snap;
    task->wait.poll_snapshot_release=nas_fs_poll_snapshot_release;
    return nas_wait_io_has_interest(task)?1:0;
}
int nas_fs_pselect_arm(uint64_t nfds,uint64_t readfds,
                       uint64_t writefds,uint64_t exceptfds) {
    struct nas_poll_snapshot *snap=nas_heap_alloc(sizeof(*snap));
    if(!snap)return -ENOMEM;
    int rc=select_import(snap,nfds,readfds,writefds,exceptfds,true);
    if(rc){nas_fs_poll_snapshot_release(snap);return rc;}
    return snapshot_arm(snap);
}
int nas_fs_poll_arm(uint64_t address,uint64_t nfds) {
    struct nas_poll_snapshot *snap=nas_heap_alloc(sizeof(*snap));
    if(!snap)return -ENOMEM;
    int rc=poll_import(snap,address,nfds,true);
    if(rc){nas_fs_poll_snapshot_release(snap);return rc;}
    return snapshot_arm(snap);
}
bool nas_fs_poll_ready(const struct nas_task *task) {
    if(!task)return false;
    if(task->wait.poll_tty_read && nas_tty_poll_readable())return true;
    if(task->wait.poll_snapshot)
        return poll_snapshot_ready(task->wait.poll_snapshot,task);
    for(unsigned slot=0;slot<NAS_OPEN_FILE_LIMIT;slot++) {
        uint16_t events=0;
        if(nas_slot_mask_test(&task->wait.poll_interest_read,slot))events|=1U;
        if(nas_slot_mask_test(&task->wait.poll_interest_write,slot))events|=4U;
        if(!events)continue;
        /* ERR/HUP must wake waiters even if the original requested
         * direction cannot make forward progress (EPIPE/EOF on replay). */
        uint16_t ready=nas_fs_poll_description_task(slot,events,task);
        if(ready&(events|0x0018U))return true;
    }
    return false;
}
