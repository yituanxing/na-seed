/* Execute the production signal.c group selection/lifetime code against a
 * deterministic 48-slot Task table. No copy of the signal implementation,
 * forged return marker, or preprocessor bypass of its group algorithms. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/task.h"
#include "../kernel/errno.h"
#include "../kernel/task/signal.h"

static struct nas_task slots[NAS_TASK_LIMIT];
static struct nas_sighand dispositions[NAS_TASK_LIMIT];
struct nas_task *nas_current_task;
static unsigned exits, cpu_restores;
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
    exits=cpu_restores=0;
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
    if(!thread)t->group_leader=t;
    else {
        for(unsigned i=0;i<NAS_TASK_LIMIT;i++)
            if(slots[i].state!=NAS_TASK_FREE && slots[i].tgid==tgid &&
               !slots[i].is_thread) {t->group_leader=&slots[i];break;}
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
    assert(eligible->pending_signals==(1ULL<<(NAS_SIGINT-1)));
    puts("M105_HOST_TTY_HANDLER_ONCE_PASS");
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
    assert(worker->pending_signals==(1ULL<<(NAS_SIGCHLD-1)));
    puts("M105_HOST_SIGCHLD_WORKER_PASS");
}
int main(void) {
    test_remote_tty_group_once();
    test_sender_in_target_group();
    test_sender_group_terminates_last();
    test_caught_exactly_once();
    test_dead_leader_live_worker();
    test_pid1_and_stop_preflight();
    test_sigchld_live_parent_worker();
    puts("M105_SIGNAL_GROUP_HOST_PASS");
    return 0;
}
