/* Execute the production signal.c group selection/lifetime code against a
 * deterministic 48-slot Task table. No copy of the signal implementation,
 * forged return marker, or preprocessor bypass of its group algorithms. */
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/task.h"
#include "../kernel/errno.h"
#include "../kernel/task/signal.h"
#include "../kernel/time/clock.h"

static struct nas_task slots[NAS_TASK_LIMIT];
static struct nas_sighand dispositions[NAS_TASK_LIMIT];
static struct nas_signal_group groups[NAS_TASK_LIMIT];
struct nas_task *nas_current_task;
static unsigned exits, cpu_restores;
static unsigned handler_installs;
static unsigned last_handler_signal;
static uint64_t observed_sigpending;
static uint8_t observed_sigwait_info[128];
static bool reject_sigwait_info;
static uint64_t incoming_action[4];
static uint64_t monotonic_ns;
void nas_print(const char *message) { fputs(message,stderr); }
_Noreturn void nas_arch_halt(void) { abort(); }
uint64_t nas_arch_monotonic_ns(void) { return monotonic_ns; }
static uint64_t exit_order[8];
static uint64_t last_exit_tgid;
static uint32_t last_exit_signal;

struct nas_task *nas_task_at(unsigned n) {
    return n<NAS_TASK_LIMIT ? &slots[n] : NULL;
}
struct nas_task *nas_task_by_pid(uint64_t pid) {
    for(unsigned i=0;i<NAS_TASK_LIMIT;i++)
        if(slots[i].state!=NAS_TASK_FREE && slots[i].pid==pid)return &slots[i];
    return NULL;
}
void nas_arch_save_fpu(struct nas_task *t) {(void)t;}
void nas_arch_restore_cpu(const struct nas_task *t) {
    /* This fails on the original P0: remote group exit freed the sender
     * then attempted to restore its now-NULL address-space identity. */
    assert(t && t->state!=NAS_TASK_FREE && t->mm && t->sighand);
    ++cpu_restores;
}
void nas_arch_save_user_frame(struct nas_task *t,const uint64_t *f) {
    (void)t;(void)f;
}
void nas_arch_set_saved_return(struct nas_task *t,uint64_t result) {
    t->arch.resume_regs[0]=result;
}
bool nas_arch_signal_install_handler(uint64_t *frame,unsigned sig,
                                      const uint64_t action[4]) {
    assert(frame && action && action[0]>1);
    ++handler_installs;
    last_handler_signal=sig;
    return true;
}
bool nas_user_write_u64(uint64_t addr,uint64_t value) {
    assert(addr==0x1000);
    observed_sigpending=value;
    return true;
}
bool nas_user_read_bytes(uint64_t addr,uint8_t *dst,uint64_t size) {
    assert(addr==0x2000 && size==sizeof(incoming_action));
    memcpy(dst,incoming_action,sizeof(incoming_action));
    return true;
}
bool nas_user_write_bytes(uint64_t addr,const uint8_t *src,uint64_t size) {
    if(addr==0x4000 && size==sizeof(observed_sigwait_info)) {
        if(reject_sigwait_info)return false;
        memcpy(observed_sigwait_info,src,size);
    }
    return true;
}
uint64_t nas_task_schedule(uint64_t *frame) {(void)frame;return 0;}
void nas_sched_thread_group_exit(uint32_t code,uint32_t signo) {
    (void)code;
    const uint64_t tgid=nas_current_task->tgid;
    ++exits;
    assert(exits<=sizeof(exit_order)/sizeof(exit_order[0]));
    exit_order[exits-1]=tgid;
    last_exit_tgid=tgid;
    last_exit_signal=signo;
    for(unsigned i=0;i<NAS_TASK_LIMIT;i++) {
        struct nas_task *t=&slots[i];
        if(t->state==NAS_TASK_FREE || t->tgid!=tgid)continue;
        if(t->is_thread && t!=nas_current_task) {
            t->state=NAS_TASK_FREE;
            t->mm=0;
            t->sighand=0;
        } else t->state=NAS_TASK_ZOMBIE;
    }
}

static void reset(void) {
    memset(slots,0,sizeof(slots));
    memset(dispositions,0,sizeof(dispositions));
    memset(groups,0,sizeof(groups));
    exits=cpu_restores=0;
    handler_installs=last_handler_signal=0;
    observed_sigpending=0;
    memset(observed_sigwait_info,0,sizeof(observed_sigwait_info));
    reject_sigwait_info=false;
    monotonic_ns=0;
    memset(incoming_action,0,sizeof(incoming_action));
    memset(exit_order,0,sizeof(exit_order));
    last_exit_tgid=0;
    last_exit_signal=0;
    nas_current_task=0;
}
static struct nas_task *make(unsigned slot,uint64_t pid,uint64_t tgid,
                             uint64_t pgid,uint64_t sid,bool thread) {
    struct nas_task *t=&slots[slot];
    t->pid=pid;t->tgid=tgid;t->ppid=1;
    t->pgid=pgid;t->sid=sid;
    t->is_thread=thread;
    t->state=NAS_TASK_BLOCKED;
    t->mm=(struct nas_mm *)(uintptr_t)(0x1000+tgid*0x1000);
    t->sighand=&dispositions[slot];
    if(!thread) {
        t->group_leader=t;
        t->signal_group=&groups[slot];
        t->signal_group->refs=1;
    }
    else {
        for(unsigned i=0;i<NAS_TASK_LIMIT;i++)
            if(slots[i].state!=NAS_TASK_FREE && slots[i].tgid==tgid &&
               !slots[i].is_thread) {
                t->group_leader=&slots[i];
                t->signal_group=slots[i].signal_group;
                ++t->signal_group->refs;
                break;
            }
        assert(t->signal_group);
    }
    return t;
}
static void test_remote_tty_group_once(void) {
    reset();
    struct nas_task *init=make(0,1,1,1,1,false);
    nas_current_task=init;
    make(1,2,2,20,3,false);
    make(2,3,2,20,3,true);
    make(3,4,2,20,3,true);
    assert(nas_signal_tty_event(3,20,NAS_SIGINT));
    assert(exits==1 && last_exit_tgid==2 && last_exit_signal==NAS_SIGINT);
    assert(slots[1].state==NAS_TASK_ZOMBIE);
    assert(slots[2].state==NAS_TASK_FREE && slots[3].state==NAS_TASK_FREE);
    assert(nas_current_task==init && cpu_restores==2);
    puts("M105_HOST_TTY_REMOTE_GROUP_PASS");
}
static void test_sender_in_target_group(void) {
    reset();
    make(0,1,1,1,1,false);
    make(1,2,2,20,3,false);
    struct nas_task *sender=make(2,3,2,20,3,true);
    make(3,4,2,20,3,true);
    nas_current_task=sender;
    assert(nas_signal_tty_event(3,20,NAS_SIGQUIT));
    assert(exits==1 && last_exit_tgid==2 && last_exit_signal==NAS_SIGQUIT);
    assert(cpu_restores==0 && nas_current_task==sender);
    assert(sender->state==NAS_TASK_ZOMBIE);
    puts("M105_HOST_TTY_LOCAL_GROUP_PASS");
}
static void test_sender_group_terminates_last(void) {
    reset();
    make(0,1,1,1,1,false);
    make(1,2,2,20,3,false);
    struct nas_task *sender=make(2,3,2,20,3,true);
    make(3,4,2,20,3,true);
    make(4,5,5,20,3,false);
    nas_current_task=sender;
    assert(nas_signal_tty_event(3,20,NAS_SIGINT));
    assert(exits==2 && exit_order[0]==5 && exit_order[1]==2);
    assert(sender->state==NAS_TASK_ZOMBIE && cpu_restores==2);
    puts("M105_HOST_TTY_SELF_LAST_PASS");
}
static void test_caught_exactly_once(void) {
    reset();
    nas_current_task=make(0,1,1,1,1,false);
    struct nas_task *leader=make(1,2,2,20,3,false);
    struct nas_task *masked=make(2,3,2,20,3,true);
    struct nas_task *eligible=make(3,4,2,20,3,true);
    masked->sighand=leader->sighand;
    eligible->sighand=leader->sighand;
    leader->sighand->actions[NAS_SIGINT][0]=0x10000; /* real handler */
    leader->blocked_signals=1ULL<<(NAS_SIGINT-1);
    masked->blocked_signals=1ULL<<(NAS_SIGINT-1);
    make(4,5,5,20,3,false); /* different TGID, same foreground PGID */
    assert(nas_signal_tty_event(3,20,NAS_SIGINT));
    assert(exits==1 && last_exit_tgid==5);
    assert(!(leader->pending_signals & (1ULL<<(NAS_SIGINT-1))));
    assert(!(masked->pending_signals & (1ULL<<(NAS_SIGINT-1))));
    assert(!eligible->pending_signals);
    assert(leader->signal_group==masked->signal_group &&
           masked->signal_group==eligible->signal_group);
    assert(eligible->signal_group->pending==(1ULL<<(NAS_SIGINT-1)));
    puts("M105_HOST_TTY_HANDLER_ONCE_PASS");
}
static void test_shared_pending_later_eligible_sibling(void) {
    reset();
    nas_current_task=make(0,1,1,1,1,false);
    struct nas_task *leader=make(1,2,2,20,3,false);
    struct nas_task *masked=make(2,3,2,20,3,true);
    struct nas_task *worker=make(3,4,2,20,3,true);
    masked->sighand=worker->sighand=leader->sighand;
    leader->sighand->actions[NAS_SIGUSR1][0]=0x12340;
    const uint64_t bit=1ULL<<(NAS_SIGUSR1-1);
    leader->blocked_signals=masked->blocked_signals=worker->blocked_signals=bit;
    assert(nas_signal_kill(2,NAS_SIGUSR1,0)==0);
    assert(leader->signal_group->pending==bit);
    assert(!leader->pending_signals && !masked->pending_signals &&
           !worker->pending_signals);
    nas_current_task=worker;
    assert(nas_signal_pending(0x1000,8)==0 && observed_sigpending==bit);
    assert(!nas_signal_user_pending(worker));
    worker->blocked_signals=0; /* unmask AFTER process-directed enqueue */
    assert(nas_signal_user_pending(worker));
    uint64_t frame[8]={0};
    nas_signal_user_return(frame);
    assert(handler_installs==1 && last_handler_signal==NAS_SIGUSR1);
    assert(!leader->signal_group->pending && !worker->pending_signals);
    assert(!nas_signal_user_pending(worker));
    assert(nas_signal_pending(0x1000,8)==0 && !observed_sigpending);
    puts("M105_HOST_GROUP_PENDING_LATE_SIBLING_PASS");
}
static void test_thread_private_and_ignored_discard(void) {
    reset();
    struct nas_task *leader=make(0,2,2,20,3,false);
    struct nas_task *worker=make(1,3,2,20,3,true);
    worker->sighand=leader->sighand;
    const uint64_t bit=1ULL<<(NAS_SIGUSR2-1);
    leader->sighand->actions[NAS_SIGUSR2][0]=0x12340;
    worker->blocked_signals=leader->blocked_signals=bit;
    nas_current_task=leader;
    assert(nas_signal_tgkill(2,3,NAS_SIGUSR2,0)==0);
    assert(worker->pending_signals==bit && !leader->pending_signals &&
           !leader->signal_group->pending);
    assert(nas_signal_kill(2,NAS_SIGUSR2,0)==0);
    assert(leader->signal_group->pending==bit && worker->pending_signals==bit);
    nas_current_task=worker;
    assert(nas_signal_pending(0x1000,8)==0 && observed_sigpending==bit);
    worker->blocked_signals=0;
    uint64_t frame[8]={0};
    nas_signal_user_return(frame);
    assert(handler_installs==1 && worker->pending_signals==0);
    assert(leader->signal_group->pending==bit); /* two ownership domains */
    worker->signal_frame_active=true; /* caller has not sigreturned yet */
    incoming_action[0]=1; /* SIG_IGN must purge both domains, every TID */
    assert(nas_signal_action(NAS_SIGUSR2,0x2000,0,8)==0);
    assert(!leader->signal_group->pending && !worker->pending_signals);
    assert(leader->sighand->actions[NAS_SIGUSR2][0]==1);
    puts("M105_HOST_THREAD_PRIVATE_IGNORE_PURGE_PASS");
}
static void test_synchronous_sigwait_consumes_exact_ownership(void) {
    reset();
    struct nas_task *leader=make(0,2,2,20,3,false);
    struct nas_task *worker=make(1,3,2,20,3,true);
    worker->sighand=leader->sighand;
    const uint64_t bit=1ULL<<(NAS_SIGUSR1-1);
    leader->blocked_signals=worker->blocked_signals=bit;
    leader->sighand->actions[NAS_SIGUSR1][0]=0x12340;
    nas_current_task=leader;
    assert(nas_signal_kill(2,NAS_SIGUSR1,0)==0);
    assert(leader->signal_group->pending==bit &&
           !leader->pending_signals && !worker->pending_signals);
    assert(nas_signal_timedwait_ready(worker,bit));
    assert(!nas_signal_timedwait_ready(worker,1ULL<<(NAS_SIGUSR2-1)));
    nas_current_task=worker;
    /* A failed usercopy never discards a signal belonging to the group. */
    reject_sigwait_info=true;
    assert(nas_signal_timedwait_consume(bit,0x4000)==(uint64_t)-EFAULT);
    assert(leader->signal_group->pending==bit);
    reject_sigwait_info=false;
    assert(nas_signal_timedwait_consume(bit,0x4000)==NAS_SIGUSR1);
    uint32_t got=0;
    memcpy(&got,observed_sigwait_info,sizeof(got));
    assert(got==NAS_SIGUSR1);
    for(unsigned i=4;i<sizeof(observed_sigwait_info);i++)
        assert(observed_sigwait_info[i]==0);
    assert(!leader->signal_group->pending &&
           nas_signal_timedwait_consume(bit,0)==(uint64_t)-EAGAIN);
    /* SIGWAIT consumes the thread's private copy *before* the TGID copy,
     * preserving two independent standard-signal ownership domains. */
    nas_current_task=leader;
    assert(nas_signal_kill(2,NAS_SIGUSR1,0)==0);
    assert(nas_signal_tgkill(2,3,NAS_SIGUSR1,0)==0);
    assert(worker->pending_signals==bit && leader->signal_group->pending==bit);
    nas_current_task=worker;
    assert(nas_signal_timedwait_consume(bit,0)==NAS_SIGUSR1);
    assert(!worker->pending_signals && leader->signal_group->pending==bit);
    assert(nas_signal_timedwait_consume(bit,0)==NAS_SIGUSR1);
    assert(!leader->signal_group->pending);
    puts("M128_HOST_SIGWAIT_TGID_TID_EFAULT_PASS");
}
static void test_posix_timer_process_vs_thread_direction(void) {
    reset();
    struct nas_task *leader=make(0,2,2,20,3,false);
    struct nas_task *worker=make(1,3,2,20,3,true);
    worker->sighand=leader->sighand;
    const uint64_t bit=1ULL<<(NAS_SIGUSR1-1);
    leader->sighand->actions[NAS_SIGUSR1][0]=0x12340;
    leader->timers[0]=(struct nas_posix_timer){
        .used=true,.armed=true,.signum=NAS_SIGUSR1,
        .clockid=1,.deadline_ns=100,
    };
    monotonic_ns=100;
    nas_time_timers_tick();
    assert(leader->signal_group->pending==bit);
    assert(!leader->pending_signals && !worker->pending_signals);
    leader->signal_group->pending=0;
    leader->timers[0]=(struct nas_posix_timer){
        .used=true,.armed=true,.signum=NAS_SIGUSR1,
        .clockid=1,.deadline_ns=200,.notify=4,.thread_tid=3,
    };
    monotonic_ns=200;
    nas_time_timers_tick();
    assert(worker->pending_signals==bit && !leader->signal_group->pending);
    puts("M105_HOST_TIMER_SIGNAL_DOMAINS_PASS");
}
static void test_dead_leader_live_worker(void) {
    reset();
    nas_current_task=make(0,1,1,1,1,false);
    struct nas_task *leader=make(1,2,2,20,3,false);
    leader->state=NAS_TASK_ZOMBIE;
    leader->group_exit_pending=true;
    make(2,3,2,20,3,true);
    assert(nas_signal_kill(2,NAS_SIGTERM,0)==0);
    assert(exits==1 && last_exit_tgid==2 && last_exit_signal==NAS_SIGTERM);
    assert(slots[2].state==NAS_TASK_ZOMBIE);
    reset();
    nas_current_task=make(0,1,1,1,1,false);
    leader=make(1,2,2,20,3,false);
    leader->state=NAS_TASK_ZOMBIE;
    leader->group_exit_pending=true;
    make(2,3,2,20,3,true);
    assert(nas_signal_kill((uint64_t)(int64_t)-20,NAS_SIGINT,0)==0);
    assert(exits==1 && last_exit_tgid==2 && last_exit_signal==NAS_SIGINT);
    puts("M105_HOST_ZOMBIE_LEADER_KILL_PASS");
}
static void test_pid1_and_stop_preflight(void) {
    reset();
    struct nas_task *init=make(0,1,1,1,1,false);
    struct nas_task *worker=make(1,2,1,1,1,true);
    worker->sighand=init->sighand;
    nas_current_task=init;
    assert(nas_signal_kill((uint64_t)(int64_t)-1,NAS_SIGSTOP,0)==(uint64_t)-ESRCH);
    assert(nas_signal_kill(1,NAS_SIGKILL,0)==(uint64_t)-EPERM);
    assert(nas_signal_tty_event(1,1,NAS_SIGINT));
    assert(exits==0 && worker->state==NAS_TASK_BLOCKED);
    make(2,3,3,20,3,false);
    struct nas_task *other=make(3,4,4,20,3,false);
    other->blocked_signals=1ULL<<(NAS_SIGTSTP-1);
    assert(!nas_signal_tty_event(3,20,NAS_SIGTSTP));
    assert(slots[2].state==NAS_TASK_BLOCKED &&
           slots[3].state==NAS_TASK_BLOCKED && exits==0);
    puts("M105_HOST_PID1_STOP_PREFLIGHT_PASS");
}
static void test_sigchld_live_parent_worker(void) {
    reset();
    make(0,1,1,1,1,false);
    struct nas_task *parent=make(1,2,2,20,3,false);
    parent->state=NAS_TASK_ZOMBIE;
    struct nas_task *worker=make(2,3,2,20,3,true);
    worker->sighand=parent->sighand;
    worker->sighand->actions[NAS_SIGCHLD][0]=0x10000;
    struct nas_task *child=make(3,4,4,20,3,false);
    child->ppid=2;
    nas_signal_child_exit(child);
    assert(worker->signal_group->pending==(1ULL<<(NAS_SIGCHLD-1)));
    assert(!worker->pending_signals);
    puts("M105_HOST_SIGCHLD_WORKER_PASS");
}
int main(void) {
    test_remote_tty_group_once();
    test_sender_in_target_group();
    test_sender_group_terminates_last();
    test_caught_exactly_once();
    test_shared_pending_later_eligible_sibling();
    test_thread_private_and_ignored_discard();
    test_synchronous_sigwait_consumes_exact_ownership();
    test_posix_timer_process_vs_thread_direction();
    test_dead_leader_live_worker();
    test_pid1_and_stop_preflight();
    test_sigchld_live_parent_worker();
    puts("M105_SIGNAL_GROUP_HOST_PASS");
    return 0;
}
