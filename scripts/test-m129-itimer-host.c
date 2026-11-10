/* Compile against the real production clock.c, not a copied timer model.
 * Exercise ITIMER_REAL ownership, snapshots, validation and SIGALRM delivery. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/task.h"
#include "../kernel/time/clock.h"
#include "../kernel/syscall/poll_wait_policy.h"
#include "../kernel/errno.h"

static uint64_t mono_ns;
static struct nas_task tasks[3];
static struct nas_signal_group group_a,group_b;
static struct nas_sighand sighand;
struct nas_task *nas_current_task;

uint64_t nas_arch_monotonic_ns(void){return mono_ns;}
struct nas_task *nas_task_at(unsigned i){
    return i<3?&tasks[i]:0;
}
struct nas_task *nas_task_by_pid(uint64_t pid){
    for(unsigned i=0;i<3;i++)if(tasks[i].pid==pid)return &tasks[i];
    return 0;
}
bool nas_user_read_bytes(uint64_t addr,uint8_t *dst,uint64_t len){
    if(!addr)return false;
    memcpy(dst,(const void *)(uintptr_t)addr,(size_t)len);
    return true;
}
bool nas_user_write_bytes(uint64_t addr,const uint8_t *src,uint64_t len){
    if(!addr)return false;
    memcpy((void *)(uintptr_t)addr,src,(size_t)len);
    return true;
}

int main(void){
    /* Real musl pause fallback on RV64/AArch64 blocks indefinitely with
     * no watched FDs. The x86 legacy pause syscall shares the same Task
     * completion, so no architecture gets a synthetic early success. */
    uint64_t poll_args[7]={0};
    assert(nas_poll_signal_only_infinite(NAS_SC_ppoll,poll_args));
    assert(!nas_poll_signal_only_infinite(NAS_SC_ppoll,
                                        (uint64_t[7]){[2]=1}));
    assert(!nas_poll_signal_only_infinite(NAS_SC_ppoll,
                                        (uint64_t[7]){[3]=123}));
    poll_args[3]=(uint64_t)-1;
    assert(nas_poll_signal_only_infinite(NAS_SC_poll,poll_args));
    poll_args[3]=0;
    assert(nas_poll_signal_only_infinite(NAS_SC_pselect6,poll_args));
    assert(!nas_poll_signal_only_infinite(NAS_SC_select,
                                        (uint64_t[7]){[1]=1}));

    group_a.refs=2;group_b.refs=1;
    tasks[0].pid=11;tasks[0].tgid=11;
    tasks[0].state=NAS_TASK_RUNNING;
    tasks[0].signal_group=&group_a;tasks[0].sighand=&sighand;
    tasks[0].group_leader=&tasks[0];
    tasks[1].pid=12;tasks[1].tgid=11;tasks[1].is_thread=true;
    tasks[1].state=NAS_TASK_READY;
    tasks[1].signal_group=&group_a;tasks[1].sighand=&sighand;
    tasks[1].group_leader=&tasks[0];
    tasks[2].pid=13;tasks[2].tgid=13;
    tasks[2].state=NAS_TASK_READY;
    tasks[2].signal_group=&group_b;tasks[2].sighand=&sighand;
    tasks[2].group_leader=&tasks[2];
    sighand.actions[14][0]=2; /* caught SIGALRM */
    nas_current_task=&tasks[0];
    mono_ns=1000000000ULL;

    uint64_t req[4]={0,250000,0,500000},old[4]={0},snap[4]={0};
    assert(nas_time_setitimer(1,(uint64_t)(uintptr_t)req,0)==(uint64_t)-EINVAL);
    assert(nas_time_getitimer(2,(uint64_t)(uintptr_t)snap)==(uint64_t)-EINVAL);
    assert(nas_time_getitimer(0,0)==(uint64_t)-EFAULT);
    req[1]=1000000;
    assert(nas_time_setitimer(0,(uint64_t)(uintptr_t)req,0)==(uint64_t)-EINVAL);
    req[1]=250000;
    assert(nas_time_setitimer(0,(uint64_t)(uintptr_t)req,
                             (uint64_t)(uintptr_t)old)==0);
    assert(old[0]==0 && old[2]==0);
    assert(nas_time_timers_armed());
    nas_time_timers_tick();
    assert(group_a.pending==0);
    assert(group_b.pending==0);
    mono_ns+=500000000ULL;
    nas_time_timers_tick();
    assert(group_a.pending==(1ULL<<13));
    assert(group_b.pending==0);
    assert(group_a.itimer_real_deadline_ns==1750000000ULL);
    group_a.pending=0;

    /* One scheduler scan after several intervals still queues ONE standard
     * signal, with the next deadline aligned to the original cadence. */
    mono_ns+=1000000000ULL;
    nas_time_timers_tick();
    assert(group_a.pending==(1ULL<<13));
    assert(group_a.itimer_real_deadline_ns==2750000000ULL);
    group_a.pending=0;

    /* A sibling can read/rearm the SAME process timer; a new fork's signal
     * group never inherits the parent's interval timer. */
    nas_current_task=&tasks[1];
    assert(nas_time_getitimer(0,(uint64_t)(uintptr_t)snap)==0);
    assert(snap[0]==0 && snap[1]==250000 && snap[2]==0 && snap[3]==250000);
    nas_current_task=&tasks[2];
    assert(nas_time_getitimer(0,(uint64_t)(uintptr_t)snap)==0);
    assert(snap[0]==0 && snap[1]==0 && snap[2]==0 && snap[3]==0);

    /* Leader zombie does not destroy timer ownership of live peer. */
    tasks[0].state=NAS_TASK_ZOMBIE;
    nas_current_task=&tasks[1];
    mono_ns+=250000000ULL;
    nas_time_timers_tick();
    assert(group_a.pending==(1ULL<<13));
    assert(group_b.pending==0);

    /* Disarming returns the previous interval and remaining duration. */
    assert(nas_time_setitimer(0,0,(uint64_t)(uintptr_t)old)==0);
    assert(old[0]==0 && old[1]==250000);
    assert(!group_a.itimer_real_armed && !nas_time_timers_armed());
    assert(nas_time_getitimer(0,(uint64_t)(uintptr_t)snap)==0);
    assert(snap[1]==0 && snap[3]==0);
    puts("M129_ITIMER_REAL_HOST_PASS");
    return 0;
}
