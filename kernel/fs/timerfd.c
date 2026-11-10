#include <stdbool.h>
#include <stdint.h>
#include "../arch/arch.h"
#include "../errno.h"
#include "../fd.h"
#include "../mm/usercopy.h"
#include "../task.h"
#include "../time/clock.h"
#include "timerfd.h"

/* Linux timerfd: clock IDs 0=REALTIME, 1=MONOTONIC, 7=BOOTTIME.
 * BOOTTIME equals MONOTONIC on NaSeed, which does not suspend the CPU.
 * No timer-specific scheduler list is needed: readiness is derived from
 * the one real clock whenever the existing Task/epoll/poll machinery probes
 * this shared File description. A parked Task is woken on ordinary timer
 * interrupts, never by a fake self-completing syscall. */
#define TFD_NONBLOCK 0x800U
#define TFD_CLOEXEC 0x80000U
#define TFD_TIMER_ABSTIME 1U
#define POLLIN 0x0001U
#define POLLRDNORM 0x0040U

struct timerfd_state {
    uint64_t deadline_ns;
    uint64_t interval_ns;
    uint64_t expirations;
    uint32_t clockid;
    bool armed;
};
static struct timerfd_state timers[NAS_OPEN_FILE_LIMIT];

static bool timerfd_now(uint32_t clockid,uint64_t *out) {
    if(clockid==1U || clockid==7U) {
        *out=nas_arch_monotonic_ns();
        return true;
    }
    return clockid==0U && nas_time_realtime_ns(out);
}
static bool timerfd_live(unsigned slot) {
    return slot<NAS_OPEN_FILE_LIMIT && nas_open_files[slot].refcount;
}
static bool timerfd_fd(uint64_t fd,unsigned *slot) {
    if(!nas_current_task || !nas_current_task->fds || fd>=NAS_FD_LIMIT ||
       nas_current_task->fds->entries[fd].kind!=FD_TIMERFD)return false;
    unsigned s=nas_current_task->fds->entries[fd].slot;
    if(!timerfd_live(s))return false;
    *slot=s;
    return true;
}
/* Advance one periodic timer mathematically even if the kernel was not
 * scheduled at each expiration. Poll/read sees a precise accumulated count. */
static void timerfd_advance(unsigned slot) {
    struct timerfd_state *t=&timers[slot];
    if(!t->armed)return;
    uint64_t now;
    if(!timerfd_now(t->clockid,&now) || now<t->deadline_ns)return;
    uint64_t n=1;
    if(t->interval_ns) {
        uint64_t skipped=(now-t->deadline_ns)/t->interval_ns;
        n=skipped==UINT64_MAX?UINT64_MAX:skipped+1;
    }
    if(UINT64_MAX-t->expirations<n)t->expirations=UINT64_MAX;
    else t->expirations+=n;
    if(!t->interval_ns) {
        t->armed=false;
        return;
    }
    if(n>(UINT64_MAX-t->deadline_ns)/t->interval_ns) {
        t->armed=false;
        return;
    }
    t->deadline_ns+=n*t->interval_ns;
}

uint64_t nas_timerfd_create(uint64_t raw_clock,uint64_t flags) {
    if(raw_clock!=0 && raw_clock!=1 && raw_clock!=7)
        return (uint64_t)-EINVAL;
    if(flags&~(TFD_NONBLOCK|TFD_CLOEXEC))return (uint64_t)-EINVAL;
    uint64_t now;
    if(!timerfd_now((uint32_t)raw_clock,&now))return (uint64_t)-EOPNOTSUPP;
    unsigned fds[2],slots[2];
    int rc=nas_fd_reserve(nas_current_task,0,1,fds,slots,"timerfd_create");
    if(rc)return (uint64_t)(int64_t)rc;
    unsigned slot=slots[0];
    timers[slot]=(struct timerfd_state){.clockid=(uint32_t)raw_clock};
    if(!nas_fd_install_new(nas_current_task,fds[0],slot,FD_TIMERFD,
                           2ULL|(flags&TFD_NONBLOCK),
                           (flags&TFD_CLOEXEC)!=0))
        return (uint64_t)-EBADF;
    return fds[0];
}

static uint64_t timerfd_snapshot(unsigned slot,uint64_t dst) {
    struct timerfd_state *t=&timers[slot];
    timerfd_advance(slot);
    uint64_t now,left=0;
    if(!timerfd_now(t->clockid,&now))return (uint64_t)-EOPNOTSUPP;
    if(t->armed && t->deadline_ns>now)left=t->deadline_ns-now;
    uint64_t spec[4]={t->interval_ns/NAS_NSEC_PER_SEC,
                      t->interval_ns%NAS_NSEC_PER_SEC,
                      left/NAS_NSEC_PER_SEC,left%NAS_NSEC_PER_SEC};
    return nas_user_write_bytes(dst,(const uint8_t *)spec,sizeof(spec))?
           0:(uint64_t)-EFAULT;
}

uint64_t nas_timerfd_gettime(uint64_t fd,uint64_t out) {
    unsigned slot;
    if(!timerfd_fd(fd,&slot))return (uint64_t)-EBADF;
    return timerfd_snapshot(slot,out);
}

uint64_t nas_timerfd_settime(uint64_t fd,uint64_t flags,
                             uint64_t new_value,uint64_t old_value) {
    unsigned slot;
    if(!timerfd_fd(fd,&slot))return (uint64_t)-EBADF;
    /* CANCEL_ON_SET has meaning only for realtime clocks and a movable
     * wall clock. NaSeed has no settimeofday/clock_settime adjustment yet. */
    if(flags&~TFD_TIMER_ABSTIME)return (uint64_t)-EINVAL;
    uint64_t spec[4],interval,start;
    if(!nas_user_read_bytes(new_value,(uint8_t *)spec,sizeof(spec)))
        return (uint64_t)-EFAULT;
    if(!nas_time_duration_ns(spec[0],spec[1],&interval) ||
       !nas_time_duration_ns(spec[2],spec[3],&start))
        return (uint64_t)-EINVAL;
    uint64_t now;
    struct timerfd_state *t=&timers[slot];
    if(!timerfd_now(t->clockid,&now))return (uint64_t)-EOPNOTSUPP;
    if(old_value) {
        uint64_t rc=timerfd_snapshot(slot,old_value);
        if((int64_t)rc<0)return rc;
    }
    t->interval_ns=interval;
    t->expirations=0;
    t->armed=start!=0;
    t->deadline_ns=t->armed ?
        (flags&TFD_TIMER_ABSTIME ? start : nas_time_add_deadline(now,start)):0;
    return 0;
}

uint64_t nas_timerfd_read_plan(unsigned slot,uint64_t count) {
    if(!timerfd_live(slot))return (uint64_t)-EBADF;
    if(count<8)return (uint64_t)-EINVAL;
    timerfd_advance(slot);
    if(!timers[slot].expirations)
        return (nas_open_files[slot].status_flags&TFD_NONBLOCK)?
            (uint64_t)-EAGAIN:(uint64_t)-EOPNOTSUPP;
    return 8;
}

uint64_t nas_timerfd_read(unsigned slot,uint8_t *dst,uint64_t count) {
    uint64_t planned=nas_timerfd_read_plan(slot,count);
    if((int64_t)planned<0)return planned;
    if(!dst)return (uint64_t)-EFAULT;
    uint64_t n=timers[slot].expirations;
    for(unsigned i=0;i<8;i++)dst[i]=(uint8_t)(n>>(i*8U));
    timers[slot].expirations=0;
    return 8;
}

uint16_t nas_timerfd_poll_slot(unsigned slot,uint16_t events) {
    if(!timerfd_live(slot))return 0;
    timerfd_advance(slot);
    return (events&POLLIN) && timers[slot].expirations ?
           POLLIN|POLLRDNORM:0;
}

uint16_t nas_timerfd_poll_fd(uint64_t fd,uint16_t events) {
    unsigned slot;
    return timerfd_fd(fd,&slot)?nas_timerfd_poll_slot(slot,events):0;
}

void nas_timerfd_release(unsigned slot) {
    if(slot<NAS_OPEN_FILE_LIMIT)timers[slot]=(struct timerfd_state){0};
}
