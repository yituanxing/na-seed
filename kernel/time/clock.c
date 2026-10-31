#include "clock.h"
#include "../arch/arch.h"
#include "../task.h"
#include "../mm/usercopy.h"
#include "../errno.h"

/* Linux clocksource/timekeeper split, intentionally single-CPU for now.
 * A Limine RTC calendar sample anchors UTC once; runtime never reads RTC
 * for durations or scheduling. Absent/implausible RTC stays unavailable.
 * No adjustable wall clock, NTP, suspend/resume or leap-second support. */
static uint64_t mono_anchor_ns;
static uint64_t realtime_anchor_ns;
static bool realtime_ready;

void nas_timekeeper_init(int64_t boot_epoch_seconds) {
    realtime_ready=false;
    /* Firmware date must be plausible, within the representable ns horizon.
     * Reject zero, negative and obviously uninitialized RTC values. */
    if(boot_epoch_seconds < INT64_C(946684800) ||
       boot_epoch_seconds > INT64_C(4102444800)) return;
    mono_anchor_ns=nas_arch_monotonic_ns();
    realtime_anchor_ns=(uint64_t)boot_epoch_seconds*NAS_NSEC_PER_SEC;
    realtime_ready=true;
}

bool nas_time_boot_epoch_seconds(uint64_t *out) {
    if(!out || !realtime_ready || realtime_anchor_ns<mono_anchor_ns)
        return false;
    *out=(realtime_anchor_ns-mono_anchor_ns)/NAS_NSEC_PER_SEC;
    return true;
}
bool nas_time_realtime_available(void) {
    return realtime_ready;
}

bool nas_time_realtime_at(uint64_t mono,uint64_t *out) {
    if(!realtime_ready || !out || mono<mono_anchor_ns) return false;
    uint64_t delta=mono-mono_anchor_ns;
    if(delta>UINT64_MAX-realtime_anchor_ns) return false;
    *out=realtime_anchor_ns+delta;
    return true;
}

bool nas_time_realtime_ns(uint64_t *out) {
    return nas_time_realtime_at(nas_arch_monotonic_ns(),out);
}

/* POSIX process timers in the existing shared timekeeper: serialised by
 * today's single-CPU non-preemptible kernel. CLOCK_MONOTONIC and REALTIME
 * use their actual independent clocks. No timer-specific ISA shortcuts. */
static struct nas_task *timer_owner(void){return nas_current_task->group_leader;}
static bool timer_now(uint32_t clock,uint64_t *out){
    if(clock==1){*out=nas_arch_monotonic_ns();return true;}
    return clock==0 && nas_time_realtime_ns(out);
}
static struct nas_posix_timer *timer_ref(uint64_t id){
    if(id>=NAS_POSIX_TIMERS || !timer_owner()->timers[id].used)return 0;
    return &timer_owner()->timers[id];
}
/* Legacy interval timers are independent of timer_create(2)'s timer-ID
 * namespace. ITIMER_REAL is a single monotonic deadline per signal group:
 * SIGALRM is process-directed, and all threads observe one replacement. */
static uint64_t itimer_real_snapshot(const struct nas_signal_group *group,
                                     uint64_t out){
    uint64_t now=nas_arch_monotonic_ns();
    uint64_t left=group->itimer_real_armed &&
        group->itimer_real_deadline_ns>now ?
        group->itimer_real_deadline_ns-now:0;
    /* Linux LP64 itimerval: interval.tv_sec, interval.tv_usec,
     * value.tv_sec, value.tv_usec; round remaining ns UP to a usec. */
    uint64_t interval=group->itimer_real_interval_ns;
    uint64_t val[4]={interval/NAS_NSEC_PER_SEC,
        (interval%NAS_NSEC_PER_SEC)/1000U,
        left/NAS_NSEC_PER_SEC,
        ((left%NAS_NSEC_PER_SEC)+999U)/1000U};
    if(val[3]==1000000U){val[2]++;val[3]=0;}
    return nas_user_write_bytes(out,(const uint8_t *)val,sizeof(val))?
        0:(uint64_t)-EFAULT;
}
uint64_t nas_time_getitimer(uint64_t which,uint64_t out){
    if(which!=0)return (uint64_t)-EINVAL; /* ITIMER_REAL only */
    if(!out)return (uint64_t)-EFAULT;
    return itimer_real_snapshot(nas_current_task->signal_group,out);
}
uint64_t nas_time_setitimer(uint64_t which,uint64_t value,uint64_t old){
    if(which!=0)return (uint64_t)-EINVAL;
    /* Linux accepts a NULL new value as a disarm. */
    uint64_t wire[4]={0};
    if(value && !nas_user_read_bytes(value,(uint8_t *)wire,sizeof(wire)))
        return (uint64_t)-EFAULT;
    if((int64_t)wire[0]<0 || (int64_t)wire[1]<0 || wire[1]>=1000000U ||
       (int64_t)wire[2]<0 || (int64_t)wire[3]<0 || wire[3]>=1000000U)
        return (uint64_t)-EINVAL;
    uint64_t interval,start;
    if(!nas_time_duration_ns(wire[0],wire[1]*1000U,&interval) ||
       !nas_time_duration_ns(wire[2],wire[3]*1000U,&start))
        return (uint64_t)-EINVAL;
    struct nas_signal_group *group=nas_current_task->signal_group;
    if(old){
        uint64_t rc=itimer_real_snapshot(group,old);
        if((int64_t)rc<0)return rc;
    }
    group->itimer_real_interval_ns=interval;
    group->itimer_real_armed=start!=0;
    group->itimer_real_deadline_ns=start?
        nas_time_add_deadline(nas_arch_monotonic_ns(),start):0;
    return 0;
}
static uint64_t timer_snapshot(const struct nas_posix_timer *t,uint64_t out){
    uint64_t now;
    if(!timer_now(t->clockid,&now))return (uint64_t)-EOPNOTSUPP;
    uint64_t left=t->armed && t->deadline_ns>now?t->deadline_ns-now:0;
    uint64_t spec[4]={t->interval_ns/NAS_NSEC_PER_SEC,
        t->interval_ns%NAS_NSEC_PER_SEC,left/NAS_NSEC_PER_SEC,
        left%NAS_NSEC_PER_SEC};
    return nas_user_write_bytes(out,(const uint8_t *)spec,sizeof(spec))
        ?0:(uint64_t)-EFAULT;
}
uint64_t nas_time_timer_create(uint64_t clock,uint64_t event,uint64_t out){
    uint64_t now;
    if(clock>1)return (uint64_t)-EINVAL;
    if(!timer_now((uint32_t)clock,&now))return (uint64_t)-EOPNOTSUPP;
    uint32_t signo=14,notify=0,tid=0;
    if(event){
        /* Linux LP64 sigevent: union sigval(8), signo(4), notify(4),
         * optional target thread ID at byte 16. */
        struct {uint64_t value;uint32_t signum,mode,thread;} ev;
        if(!nas_user_read_bytes(event,(uint8_t *)&ev,sizeof(ev)))
            return (uint64_t)-EFAULT;
        signo=ev.signum;notify=ev.mode;tid=ev.thread;
        if(notify!=0 && notify!=1 && notify!=4)
            return (uint64_t)-EOPNOTSUPP; /* SIGEV_THREAD requires userspace helper */
        if(notify!=1 && (!signo || signo>64 || signo==9 || signo==19))
            return (uint64_t)-EINVAL;
        if(notify==4){
            struct nas_task *dst=nas_task_by_pid(tid);
            if(!dst || dst->tgid!=timer_owner()->tgid)return (uint64_t)-EINVAL;
        }
    }
    for(unsigned i=0;i<NAS_POSIX_TIMERS;i++){
        struct nas_posix_timer *t=&timer_owner()->timers[i];
        if(t->used)continue;
        uint32_t id=i;
        if(!nas_user_write_bytes(out,(const uint8_t *)&id,4))
            return (uint64_t)-EFAULT;
        *t=(struct nas_posix_timer){0};
        t->used=true;t->clockid=(uint32_t)clock;t->signum=signo;
        t->notify=(uint8_t)notify;t->thread_tid=tid;
        return 0;
    }
    return (uint64_t)-EAGAIN;
}
uint64_t nas_time_timer_settime(uint64_t id,uint64_t flags,uint64_t req,uint64_t old){
    struct nas_posix_timer *t=timer_ref(id);
    if(!t)return (uint64_t)-EINVAL;
    if(flags&~1ULL)return (uint64_t)-EINVAL;
    uint64_t val[4];
    if(!nas_user_read_bytes(req,(uint8_t *)val,sizeof(val)))
        return (uint64_t)-EFAULT;
    uint64_t interval,start;
    if(!nas_time_duration_ns(val[0],val[1],&interval) ||
       !nas_time_duration_ns(val[2],val[3],&start))
        return (uint64_t)-EINVAL;
    uint64_t now;
    if(!timer_now(t->clockid,&now))return (uint64_t)-EOPNOTSUPP;
    if(old){
        uint64_t rc=timer_snapshot(t,old);
        if((int64_t)rc<0)return rc;
    }
    t->interval_ns=interval;t->armed=start!=0;t->overrun=0;
    t->deadline_ns=t->armed?((flags&1ULL)?start:nas_time_add_deadline(now,start)):0;
    return 0;
}
uint64_t nas_time_timer_gettime(uint64_t id,uint64_t out){
    struct nas_posix_timer *t=timer_ref(id);
    return t?timer_snapshot(t,out):(uint64_t)-EINVAL;
}
uint64_t nas_time_timer_getoverrun(uint64_t id){
    struct nas_posix_timer *t=timer_ref(id);
    return t?(uint64_t)(uint32_t)t->overrun:(uint64_t)-EINVAL;
}
uint64_t nas_time_timer_delete(uint64_t id){
    struct nas_posix_timer *t=timer_ref(id);
    if(!t)return (uint64_t)-EINVAL;
    *t=(struct nas_posix_timer){0};
    return 0;
}
void nas_time_timers_drop(void){
    for(unsigned n=0;n<NAS_POSIX_TIMERS;n++)
        timer_owner()->timers[n]=(struct nas_posix_timer){0};
}
bool nas_time_timers_armed(void){
    for(unsigned i=0;i<NAS_TASK_LIMIT;i++){
        const struct nas_task *p=nas_task_at(i);
        if(!p || !p->pid || p->state==NAS_TASK_FREE ||
           p->state==NAS_TASK_ZOMBIE || p->is_thread)continue;
        if(p->signal_group && p->signal_group->itimer_real_armed)return true;
        for(unsigned j=0;j<NAS_POSIX_TIMERS;j++)
            if(p->timers[j].used && p->timers[j].armed)return true;
    }
    /* A leader may exit while CLONE_THREAD workers still own the ITIMER.
     * Its signal-group deadline remains live until the last member exits. */
    for(unsigned i=0;i<NAS_TASK_LIMIT;i++){
        const struct nas_task *p=nas_task_at(i);
        if(p && p->pid && p->state!=NAS_TASK_FREE &&
           p->state!=NAS_TASK_ZOMBIE && p->signal_group &&
           p->signal_group->itimer_real_armed)return true;
    }
    return false;
}
void nas_time_timers_tick(void){
    for(unsigned i=0;i<NAS_TASK_LIMIT;i++){
        struct nas_task *p=nas_task_at(i);
        if(!p || !p->pid || p->state==NAS_TASK_FREE ||
           p->state==NAS_TASK_ZOMBIE || p->is_thread)continue;
        for(unsigned j=0;j<NAS_POSIX_TIMERS;j++){
            struct nas_posix_timer *t=&p->timers[j];
            if(!t->used || !t->armed)continue;
            uint64_t now;
            if(!timer_now(t->clockid,&now) || now<t->deadline_ns)continue;
            if(!t->interval_ns)t->armed=false;
            else {
                uint64_t late=now-t->deadline_ns;
                uint64_t skipped=late/t->interval_ns;
                t->overrun=(int32_t)(skipped>2147483647U?2147483647U:skipped);
                uint64_t steps=skipped+1;
                if(steps>(UINT64_MAX-t->deadline_ns)/t->interval_ns)t->armed=false;
                else t->deadline_ns+=steps*t->interval_ns;
            }
            if(t->notify==1)continue;
            struct nas_task *dst=t->notify==4?
                nas_task_by_pid(t->thread_tid):p;
            if(dst && dst->state!=NAS_TASK_FREE &&
               dst->state!=NAS_TASK_ZOMBIE &&
               dst->tgid==p->tgid &&
               p->sighand->actions[t->signum][0]!=1) {
                /* SIGEV_SIGNAL is process-directed; SIGEV_THREAD_ID is
                 * explicitly TID-directed. Their pending domains must not
                 * collapse just because the timer lives on the leader. */
                if(t->notify==4)dst->pending_signals|=1ULL<<(t->signum-1);
                else if(p->signal_group)
                    p->signal_group->pending|=1ULL<<(t->signum-1);
            }
        }
    }
    /* One firing per process, even with N runnable sibling threads. The
     * signal-group is the existing shared SIGALRM pending owner; this does
     * not invent a separate signal queue or duplicate signals per TID. */
    uint64_t now=nas_arch_monotonic_ns();
    for(unsigned i=0;i<NAS_TASK_LIMIT;i++){
        struct nas_task *p=nas_task_at(i);
        if(!p || !p->pid || p->state==NAS_TASK_FREE ||
           p->state==NAS_TASK_ZOMBIE || !p->signal_group)continue;
        bool first=true;
        for(unsigned k=0;k<i;k++){
            const struct nas_task *q=nas_task_at(k);
            if(q && q->pid && q->state!=NAS_TASK_FREE &&
               q->state!=NAS_TASK_ZOMBIE &&
               q->signal_group==p->signal_group){first=false;break;}
        }
        if(!first)continue;
        struct nas_signal_group *group=p->signal_group;
        if(!group->itimer_real_armed ||
           now<group->itimer_real_deadline_ns)continue;
        if(!group->itimer_real_interval_ns)group->itimer_real_armed=false;
        else {
            uint64_t missed=(now-group->itimer_real_deadline_ns)/
                group->itimer_real_interval_ns;
            uint64_t steps=missed+1;
            if(steps>(UINT64_MAX-group->itimer_real_deadline_ns)/
                      group->itimer_real_interval_ns)
                group->itimer_real_armed=false;
            else group->itimer_real_deadline_ns+=
                     steps*group->itimer_real_interval_ns;
        }
        /* SIG_IGN discards expirations; blocked signals still remain in
         * the shared pending domain until a thread unblocks them. */
        if(p->sighand->actions[14][0]!=1)
            group->pending|=1ULL<<(14-1); /* SIGALRM */
    }
}
