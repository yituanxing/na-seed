/* Run the production signal.c shutdown gate, not a duplicated Task traversal.
 * Model final close on SIGKILL to assert all three BusyBox timeout /dev/null
 * references vanish BEFORE the strict VFS teardown is allowed to run. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/task.h"
#include "../kernel/task/signal.h"
#include "../kernel/errno.h"

static struct nas_task tasks[NAS_TASK_LIMIT];
static struct nas_mm mms[NAS_TASK_LIMIT];
static struct nas_sighand sighands[NAS_TASK_LIMIT];
static struct nas_signal_group groups[NAS_TASK_LIMIT];
static unsigned dev_refs[NAS_TASK_LIMIT];
static unsigned open_dev_refs, open_mmap_refs, killed_groups, cpu_restores;
struct nas_task *nas_current_task;
void nas_print(const char *message) { fputs(message,stderr); }
void nas_dec(uint64_t value) {(void)value;}
void nas_hex(uint64_t value) {(void)value;}
void nas_x86_debug_recent_syscalls(uint64_t pid) {(void)pid;}
_Noreturn void nas_arch_halt(void) { abort(); }
bool nas_heap_free(void *memory) { free(memory); return true; }
void nas_fs_mmap_put(struct nas_mapped_file *file) {
    assert(file && open_mmap_refs);
    --open_mmap_refs;
}

struct nas_task *nas_task_at(unsigned slot) {
    return slot<NAS_TASK_LIMIT ? &tasks[slot] : NULL;
}
void nas_arch_save_fpu(struct nas_task *t) {(void)t;}
void nas_arch_restore_cpu(const struct nas_task *t) {
    assert(t && t->state!=NAS_TASK_FREE && t->mm && t->sighand);
    cpu_restores++;
}
void nas_arch_save_user_frame(struct nas_task *t,const uint64_t *f) {
    (void)t;(void)f;
}
void nas_arch_set_saved_return(struct nas_task *t,uint64_t result) {
    (void)t;(void)result;
}
uint64_t nas_task_schedule(uint64_t *frame) {
    (void)frame;
    abort(); /* shutdown must NEVER schedule from an already-dead PID1 */
}
void nas_sched_thread_group_exit(uint32_t code,uint32_t signo) {
    assert(code==0 && signo==NAS_SIGKILL);
    const uint64_t tgid=nas_current_task->tgid;
    assert(tgid!=1);
    killed_groups++;
    for(unsigned slot=0;slot<NAS_TASK_LIMIT;slot++) {
        struct nas_task *t=&tasks[slot];
        if(t->state==NAS_TASK_FREE || t->tgid!=tgid)continue;
        assert(open_dev_refs>=dev_refs[slot]);
        open_dev_refs-=dev_refs[slot];
        dev_refs[slot]=0;
        t->state=NAS_TASK_ZOMBIE;
    }
}

static struct nas_task *task(unsigned slot,uint64_t pid,uint64_t tgid,
                             enum nas_task_state state,bool thread) {
    struct nas_task *t=&tasks[slot];
    t->pid=pid;t->tgid=tgid;t->ppid=1;
    t->pgid=tgid;t->sid=1;
    t->state=state;t->is_thread=thread;
    t->mm=&mms[slot];
    t->sighand=&sighands[slot];
    if(!thread) {
        t->group_leader=t;
        t->signal_group=&groups[slot];
        t->signal_group->refs=1;
    } else {
        for(unsigned j=0;j<slot;j++)
            if(tasks[j].state!=NAS_TASK_FREE && tasks[j].tgid==tgid &&
               !tasks[j].is_thread) {
                t->group_leader=&tasks[j];
                t->signal_group=tasks[j].signal_group;
                t->signal_group->refs++;
                break;
            }
        assert(t->group_leader);
    }
    return t;
}
static void reset(void) {
    memset(tasks,0,sizeof(tasks));
    memset(mms,0,sizeof(mms));
    memset(sighands,0,sizeof(sighands));
    memset(groups,0,sizeof(groups));
    memset(dev_refs,0,sizeof(dev_refs));
    open_dev_refs=open_mmap_refs=killed_groups=cpu_restores=0;
    nas_current_task=task(0,1,1,NAS_TASK_RUNNING,false);
}

/* The real VMA release logic owns a file lease for every lazy private
 * mmap slot; a zombie still retains its MM root until wait4. */
static void pin_zombie_mmaps(struct nas_task *owner,unsigned count) {
    struct nas_anon_regions *vmas=&owner->mm->mmap_vmas;
    assert(!vmas->slots && count);
    vmas->slots=calloc(count,sizeof(*vmas->slots));
    assert(vmas->slots);
    vmas->capacity=count;
    for(unsigned i=0;i<count;i++) {
        vmas->slots[i].start=0x20000000ULL+i*4096U;
        vmas->slots[i].end=vmas->slots[i].start+4096U;
        vmas->slots[i].kind=NAS_VMA_FILE_PRIVATE_LAZY;
        vmas->slots[i].backing=(struct nas_mapped_file *)(uintptr_t)(i+1U);
        ++open_mmap_refs;
    }
}

int main(void) {
    reset();
    task(1,2,2,NAS_TASK_BLOCKED,false);
    dev_refs[1]=3;
    open_dev_refs=3;
    /* Ordinary PID1 execution is absolutely not an OS-wide shutdown. */
    assert(nas_signal_shutdown_users()==-EPERM);
    assert(open_dev_refs==3 && killed_groups==0);
    nas_current_task->state=NAS_TASK_ZOMBIE;
    nas_current_task->group_exit_pending=true;
    assert(nas_signal_shutdown_users()==-EPERM);
    nas_current_task->group_exit_pending=false;
    /* Watchdog owns 3 /dev/null OpenFiles; signal cleanup drops all three. */
    assert(nas_signal_shutdown_users()==0);
    assert(open_dev_refs==0 && killed_groups==1 && cpu_restores==2);
    assert(tasks[1].state==NAS_TASK_ZOMBIE);
    assert(nas_signal_shutdown_users()==0 && killed_groups==1);
    puts("M191_PID1_WATCHDOG_DEV_PIN_PASS");

    reset();
    nas_current_task->state=NAS_TASK_ZOMBIE;
    task(1,10,10,NAS_TASK_ZOMBIE,false); /* leader exited first */
    task(2,11,10,NAS_TASK_BLOCKED,true);
    task(3,20,20,NAS_TASK_STOPPED,false);
    task(4,30,30,NAS_TASK_READY,false);
    task(5,40,40,NAS_TASK_ZOMBIE,false); /* already dead, never killed */
    dev_refs[2]=2;dev_refs[3]=1;dev_refs[4]=1;
    open_dev_refs=4;
    assert(nas_signal_shutdown_users()==0);
    assert(killed_groups==3 && cpu_restores==6 && open_dev_refs==0);
    assert(tasks[0].state==NAS_TASK_ZOMBIE);
    assert(tasks[1].state==NAS_TASK_ZOMBIE);
    assert(tasks[2].state==NAS_TASK_ZOMBIE);
    assert(tasks[3].state==NAS_TASK_ZOMBIE);
    assert(tasks[4].state==NAS_TASK_ZOMBIE);
    assert(tasks[5].state==NAS_TASK_ZOMBIE);
    puts("M191_PID1_BLOCKED_STOPPED_THREADS_PASS");

    reset();
    nas_current_task->state=NAS_TASK_ZOMBIE;
    assert(nas_signal_shutdown_users()==0);
    assert(!killed_groups && !cpu_restores);
    puts("M191_PID1_EMPTY_QUIESCE_PASS");

    reset();
    /* Original Debian glibc PID1 leaves loader/libc private file VMAs;
     * unreaped subprocesses can leave their own VMA pins as well. */
    pin_zombie_mmaps(nas_current_task,2);
    struct nas_task *child=task(1,2,2,NAS_TASK_ZOMBIE,false);
    pin_zombie_mmaps(child,1);
    /* CLONE_VM zombies may point at the same ledger; release once. */
    struct nas_task *shared=task(2,3,3,NAS_TASK_ZOMBIE,false);
    shared->mm=nas_current_task->mm;
    assert(nas_signal_shutdown_users()==-EPERM);
    assert(open_mmap_refs==3);
    nas_current_task->state=NAS_TASK_ZOMBIE;
    assert(nas_signal_shutdown_users()==0);
    assert(open_mmap_refs==0);
    assert(!nas_current_task->mm->mmap_vmas.slots);
    assert(!child->mm->mmap_vmas.slots);
    assert(nas_signal_shutdown_users()==0 && open_mmap_refs==0);
    puts("M249_GLIBC_ZOMBIE_MMAP_UNMOUNT_PASS");
    return 0;
}
