/* M111: exercise the REAL process.c getrusage/wait4 accounting helpers.
 * Scheduler clocks and untrusted usercopy are mocked, not the rusage code. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/task.h"
#include "../kernel/task/process.h"
#include "../kernel/task/rusage.h"
#include "../kernel/task/user_stack.h"
#include "../kernel/errno.h"

static struct nas_task tasks[NAS_TASK_LIMIT];
struct nas_task *nas_current_task;
static struct nas_linux_rusage last_usage;
static uint64_t last_limit[2],requested_limit[2];
static uint64_t last_length;
static unsigned write_count,clock_samples,releases;
static bool reject_write;

struct nas_task *nas_task_at(unsigned slot) {
    assert(slot<NAS_TASK_LIMIT);
    return &tasks[slot];
}
void nas_task_cpu_account_snapshot(uint64_t *busy,uint64_t *idle,
                                   uint64_t load[3],uint64_t *switches) {
    (void)busy;(void)idle;(void)load;(void)switches;
    clock_samples++;
}
bool nas_user_write_bytes(uint64_t address,const uint8_t *src,uint64_t len) {
    write_count++;
    last_length=len;
    if(reject_write || address!=0x7000)return false;
    if(len==sizeof(last_usage))
        __builtin_memcpy(&last_usage,src,len);
    else if(len==sizeof(last_limit))
        __builtin_memcpy(last_limit,src,len);
    else if(len!=4)assert(!"invalid rusage/status usercopy length");
    return true;
}
bool nas_user_read_bytes(uint64_t address,uint8_t *dst,uint64_t len) {
    if(address!=0x8000 || len!=sizeof(requested_limit))return false;
    __builtin_memcpy(dst,requested_limit,len);
    return true;
}
struct nas_task *nas_task_by_pid(uint64_t pid) {
    for(unsigned i=0;i<NAS_TASK_LIMIT;i++)
        if(tasks[i].state!=NAS_TASK_FREE && tasks[i].pid==pid)
            return &tasks[i];
    return 0;
}
void nas_task_release(struct nas_task *task) {
    assert(task && task!=nas_current_task);
    task->state=NAS_TASK_FREE;
    releases++;
}
void nas_print(const char *s) {(void)s;}
void nas_dec(uint64_t n) {(void)n;}

static void setup(void) {
    for(unsigned i=0;i<NAS_TASK_LIMIT;i++)tasks[i]=(struct nas_task){0};
    nas_current_task=&tasks[0];
    nas_current_task->state=NAS_TASK_RUNNING;
    nas_current_task->pid=nas_current_task->tgid=nas_current_task->pgid=1;
    nas_current_task->group_leader=nas_current_task;
    last_usage=(struct nas_linux_rusage){0};
    last_length=write_count=clock_samples=releases=0;
    reject_write=false;
}
int main(void) {
    setup();
    struct nas_task *leader=&tasks[0], *sibling=&tasks[1];
    leader->cpu_runtime_ns=1500000000ULL;
    leader->exited_thread_cpu_ns=400000000ULL;
    sibling->state=NAS_TASK_BLOCKED;
    sibling->is_thread=true;
    sibling->tgid=1;
    sibling->cpu_runtime_ns=2100000000ULL;
    assert(nas_process_getrusage(0,0x7000)==0); /* SELF */
    assert(last_length==144 && last_usage.utime.sec==4 &&
           last_usage.utime.usec==0 && last_usage.stime.sec==0);
    assert(nas_process_getrusage(1,0x7000)==0); /* THREAD */
    assert(last_usage.utime.sec==1 && last_usage.utime.usec==500000);
    assert(nas_process_getrusage((uint64_t)-1,0x7000)==0); /* CHILDREN */
    assert(last_usage.utime.sec==0 && last_usage.utime.usec==0);
    assert(clock_samples==3);
    unsigned previous=write_count;
    assert((int64_t)nas_process_getrusage(2,0x7000)==-EINVAL);
    assert(write_count==previous);
    assert((int64_t)nas_process_getrusage(0,0)==-EFAULT);
    reject_write=true;
    assert((int64_t)nas_process_getrusage(0,0x7000)==-EFAULT);
    reject_write=false;

    /* Child + its already-released threads + successfully waited children.
     * No RUSAGE_CHILDREN credit is issued on failed wait4 usercopy. */
    struct nas_task *child=&tasks[2];
    child->state=NAS_TASK_ZOMBIE;
    child->pid=child->tgid=2;
    child->ppid=1;
    child->pgid=1;
    child->cpu_runtime_ns=2000000000ULL;
    child->exited_thread_cpu_ns=1000000000ULL;
    child->waited_children_cpu_ns=500000000ULL;
    reject_write=true;
    assert((int64_t)nas_process_wait4((uint64_t)-1,0x7000,0,0)==-EFAULT);
    assert(child->state==NAS_TASK_ZOMBIE && leader->waited_children_cpu_ns==0);
    reject_write=false;
    assert(nas_process_wait4((uint64_t)-1,0,0,0x7000)==2);
    assert(last_usage.utime.sec==3 && last_usage.utime.usec==0);
    assert(releases==1 && child->state==NAS_TASK_FREE);
    assert(leader->waited_children_cpu_ns==3500000000ULL);
    assert(nas_process_getrusage((uint64_t)-1,0x7000)==0);
    assert(last_usage.utime.sec==3 && last_usage.utime.usec==500000);
    assert(nas_rusage_cpu_add(UINT64_MAX,1)==UINT64_MAX);
    /* M127: the exposed Linux RLIMIT_STACK must match the 8-MiB VMA
     * reservation, not the stale 128-KiB prlimit report. */
    assert(nas_process_prlimit(0,3,0,0x7000)==0);
    assert(last_limit[0]==NAS_USER_STACK_RESERVE_PAGES*4096ULL &&
           last_limit[1]==NAS_USER_STACK_RESERVE_PAGES*4096ULL);
    requested_limit[0]=last_limit[0];
    requested_limit[1]=last_limit[1];
    assert(nas_process_prlimit(0,3,0x8000,0x7000)==0);
    requested_limit[0]-=4096;
    assert((int64_t)nas_process_prlimit(0,3,0x8000,0)==-EOPNOTSUPP);
    assert(nas_process_prlimit(0,3,0,0x7000)==0 &&
           last_limit[0]==NAS_USER_STACK_RESERVE_PAGES*4096ULL);
    puts("[M111 HYPERFINE RUSAGE HOST PASS] Linux LP64 ABI, SELF/THREAD/CHILDREN, completed thread and waited child accounting, EFAULT rollback");
    puts("[M127 RLIMIT_STACK HOST PASS] prlimit64 reports 8MiB stack reservation and rejects unenforced changes");
    return 0;
}
