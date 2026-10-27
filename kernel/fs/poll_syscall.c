/* Kernel-owned poll interests and shared Linux poll/select ABI. */
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
#include "../pipe.h"
#include "../time/clock.h"
#include "../drivers/tty.h"
#include "../drivers/pty.h"
#include "file_ops.h"
#include "../task/wait.h"
#include "unix_socket.h"
#define TASK (*nas_current_task)

/* Kernel Linux pollfd ABI: int32 fd, int16 events, int16 revents.
 * The only supported blocking timeout is infinite; positive timeouts need
 * monotonic deadline ownership is provided by the Task scheduler.
 * Readiness and the snapshot are distinct: pollfd copy-out is in the
 * owning task's VM, scheduler tests only a kernel-owned interest snapshot.
 */
uint64_t nas_fs_poll(uint64_t address, uint64_t nfds, uint64_t timeout) {
    const uint16_t POLLNVAL = 0x0020;
    if (nfds > 64) return (uint64_t)-EINVAL;
    if (nfds && (address >= USER_LIMIT || nfds > (USER_LIMIT - address) / 8))
        return (uint64_t)-EFAULT;
    uint64_t ready = 0;
    for (uint64_t i = 0; i < nfds; ++i) {
        uint64_t word;
        if (!nas_user_read_u64(address + i * 8, &word)) return (uint64_t)-EFAULT;
        int32_t fd = (int32_t)(uint32_t)word;
        uint16_t events = (uint16_t)(word >> 32);
        uint16_t revents = 0;
        if (fd >= 0) {
            if (fd >= 64 || TASK.fds->entries[(unsigned)fd].kind == FD_CLOSED)
                revents = POLLNVAL;
            else {
                const struct nas_file_ops *ops=nas_open_files[TASK.fds->entries[(unsigned)fd].slot].ops;
                revents=ops && ops->poll ? ops->poll((uint64_t)(unsigned)fd,events) : 0;
            }
        }
        if (revents) ready++;
        word = (word & 0x0000ffffffffffffULL) | ((uint64_t)revents << 48);
        if (!nas_user_write_u64(address + i * 8, word)) return (uint64_t)-EFAULT;
    }
    int32_t ms=(int32_t)(uint32_t)timeout;
    if(ready || ms==0)return ready;
    /* Deadline belongs to the current Task and survives syscall replay;
     * zero return is reached only after the hardware clock truly advances. */
    if(TASK.wait.deadline_active &&
       nas_time_deadline_expired(nas_arch_monotonic_ns(),TASK.wait.deadline_ns))
        return 0;
    return (uint64_t)-EAGAIN; /* dispatcher arms actual I/O or timer wait */
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
static bool select_read_bits(uint64_t ptr,uint64_t *mask) {
    *mask=0;
    return !ptr || nas_user_read_u64(ptr,mask);
}
uint64_t nas_fs_pselect6(uint64_t nfds,uint64_t readfds,
                         uint64_t writefds,uint64_t exceptfds,
                         bool immediate) {
    if(nfds>NAS_FD_LIMIT)return (uint64_t)-EINVAL;
    uint64_t readset,writeset,exceptset;
    if(!select_read_bits(readfds,&readset) ||
       !select_read_bits(writefds,&writeset) ||
       !select_read_bits(exceptfds,&exceptset))
        return (uint64_t)-EFAULT;
    const uint64_t limit=nfds==64?UINT64_MAX:((1ULL<<nfds)-1);
    readset &= limit; writeset &= limit; exceptset &= limit;
    uint64_t readready=0,writeready=0,exceptready=0;
    uint64_t count=0;
    for(unsigned fd=0;fd<nfds;fd++) {
        uint64_t bit=1ULL<<fd;
        uint16_t events=0;
        if(readset&bit)events|=1U;      /* POLLIN */
        if(writeset&bit)events|=4U;     /* POLLOUT */
        if(exceptset&bit)events|=2U;    /* POLLPRI */
        if(!events)continue;
        if(TASK.fds->entries[fd].kind==FD_CLOSED)return (uint64_t)-EBADF;
        const struct nas_file_ops *ops=nas_open_files[TASK.fds->entries[fd].slot].ops;
        uint16_t revents=ops && ops->poll ? ops->poll(fd,events) : 0;
        bool any=false;
        if((readset&bit) && (revents&(1U|8U|16U))) {
            readready|=bit;any=true;
        }
        if((writeset&bit) && (revents&(4U|8U|16U))) {
            writeready|=bit;any=true;
        }
        if((exceptset&bit) && (revents&2U)) {
            exceptready|=bit;any=true;
        }
        if(any)count++;
    }
    if(!count && !immediate &&
       !(TASK.wait.deadline_active &&
         nas_time_deadline_expired(nas_arch_monotonic_ns(),TASK.wait.deadline_ns)))
        return (uint64_t)-EAGAIN;
    if((readfds && !nas_user_write_u64(readfds,readready)) ||
       (writefds && !nas_user_write_u64(writefds,writeready)) ||
       (exceptfds && !nas_user_write_u64(exceptfds,exceptready)))
        return (uint64_t)-EFAULT;
    return count;
}
bool nas_fs_pselect_arm(uint64_t nfds,uint64_t readfds,
                        uint64_t writefds,uint64_t exceptfds) {
    struct nas_task *task=nas_current_task;
    nas_wait_io_clear(task);
    uint64_t readset,writeset,exceptset;
    if(nfds>NAS_FD_LIMIT ||
       !select_read_bits(readfds,&readset) ||
       !select_read_bits(writefds,&writeset) ||
       !select_read_bits(exceptfds,&exceptset))return false;
    for(unsigned fd=0;fd<nfds;fd++) {
        uint64_t bit=1ULL<<fd;
        uint16_t events=0;
        if(readset&bit)events|=1U;
        if(writeset&bit)events|=4U;
        if(exceptset&bit)events|=2U;
        if(!events || TASK.fds->entries[fd].kind==FD_CLOSED)continue;
        unsigned slot=TASK.fds->entries[fd].slot;
        if(slot>=NAS_OPEN_FILE_LIMIT)continue;
        nas_wait_io_arm(task,slot,events,
                        (TASK.fds->entries[fd].kind==FD_TTY ||
                         TASK.fds->entries[fd].kind==FD_UART) &&
                        nas_tty_read_waitable());
    }
    return nas_wait_io_has_interest(task);
}
bool nas_fs_poll_arm(uint64_t address,uint64_t nfds) {
    struct nas_task *task=nas_current_task;
    nas_wait_io_clear(task);
    if(nfds>NAS_FD_LIMIT)return false; /* nas_fs_poll checked bounds */
    for(uint64_t i=0;i<nfds;i++) {
        uint64_t raw;
        if(!nas_user_read_u64(address+i*8,&raw))return false;
        int32_t fd=(int32_t)(uint32_t)raw;
        uint16_t events=(uint16_t)(raw>>32);
        if(fd<0 || fd>=NAS_FD_LIMIT || !events)continue;
        uint8_t kind=TASK.fds->entries[(unsigned)fd].kind;
        if(kind==FD_CLOSED)continue;
        unsigned slot=TASK.fds->entries[(unsigned)fd].slot;
        if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount)
            continue;
        nas_wait_io_arm(task,slot,events,
                        (kind==FD_TTY || kind==FD_UART) &&
                        nas_tty_read_waitable());
    }
    return nas_wait_io_has_interest(task);
}
bool nas_fs_poll_ready(const struct nas_task *task) {
    if(!task)return false;
    if(task->wait.poll_tty_read && nas_tty_poll_readable())return true;
    for(unsigned slot=0;slot<NAS_OPEN_FILE_LIMIT;slot++) {
        uint16_t events=0;
        if(nas_slot_mask_test(&task->wait.poll_interest_read,slot))events|=1U;
        if(nas_slot_mask_test(&task->wait.poll_interest_write,slot))events|=4U;
        if(!events)continue;
        /* ERR/HUP must wake waiters even if the original requested
         * direction cannot make forward progress (EPIPE/EOF on replay). */
        uint16_t ready=nas_fs_poll_description(slot,events);
        if(ready&(events|0x0018U))return true;
    }
    return false;
}
