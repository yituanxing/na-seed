/* M103: real process.c Linux clone/clear_child_tid/robust-futex lifecycle.
 * User pointers are untrusted: put_user failures must never kill the
 * kernel or strand a child whose PID was already returned to its parent.
 * Linux's oracle is schedule_tail() + mm_release() in sched/core.c/fork.c. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/task.h"
#include "../kernel/task/process.h"
#include "../kernel/arch/arch.h"
#include "../kernel/arch/mmu.h"

static struct nas_mm parent_mm,child_mm;
static struct nas_files parent_files,child_files;
static struct nas_fs_context parent_fs,child_fs;
static struct nas_sighand parent_sighand,child_sighand;
static struct nas_task parent,child;
struct nas_task *nas_current_task;

static uint64_t next_pid=101;
static uint64_t last_written_address,last_written_mm,last_wake_address;
static uint32_t last_written_word;
static unsigned write_attempts,successful_writes,wakes,panic_count;
static unsigned allocated_children,released_children;
static bool reject_user_write;
static uint64_t last_parent_return,last_child_return;
static uint64_t robust_head=0x4000,robust_node=0x5000;
static uint64_t robust_next=0x5000,robust_pending;
static uint32_t robust_word;
static bool reject_robust_write;

void nas_print(const char *s) {(void)s;}
void nas_dec(uint64_t x) {(void)x;}
void nas_hex(uint64_t x) {(void)x;}
_Noreturn void nas_arch_halt(void) {
    ++panic_count;
    assert(!"kernel panic on user-controlled TID/futex address");
    for(;;) {}
}
void nas_arch_save_user_frame(struct nas_task *t,const uint64_t *frame) {
    assert(t==&parent && frame);
}
void nas_arch_save_fpu(struct nas_task *t) {assert(t==&parent);}
void nas_arch_set_saved_return(struct nas_task *t,uint64_t value) {
    if(t==&parent)last_parent_return=value;
    else {assert(t==&child);last_child_return=value;}
}
uint64_t nas_task_schedule(uint64_t *frame) {
    (void)frame;
    assert(!"ptrace stop unexpectedly scheduled in child TID test");
    return 0;
}
bool nas_ptrace_fork_created(struct nas_task *p,struct nas_task *c,
                             unsigned event,uint64_t *frame) {
    (void)event;(void)frame;
    assert(p==&parent && c==&child);
    return false;
}
struct nas_task *nas_task_fork_slot(void) {
    ++allocated_children;
    child=(struct nas_task){0};
    child.pid=next_pid++;
    child.tgid=child.pid;
    child.state=NAS_TASK_READY;
    child.mm=0;
    child.group_leader=&child;
    child.files=&child_files;
    child.fds=&child_files.table;
    child.fs=&child_fs;
    child.sighand=&child_sighand;
    /* Real task_fork_slot copies the architecture state before overrides. */
    child.arch=parent.arch;
    /* The real fork-slot memcpy briefly transfers these raw addresses;
     * every process creation path must explicitly replace stale values. */
    child.thread_set_tid=parent.thread_set_tid;
    child.thread_clear_tid=parent.thread_clear_tid;
    return &child;
}
struct nas_mm *nas_mm_fork_cow(const struct nas_mm *mm) {
    assert(mm==&parent_mm);
    return &child_mm;
}
struct nas_mm *nas_mm_get(struct nas_mm *mm) {
    assert(mm==&parent_mm);
    return mm;
}
bool nas_task_hold_files(const struct nas_task *t) {assert(t==&child);return true;}
void nas_task_release(struct nas_task *t) {
    assert(t==&child && t!=nas_current_task);
    t->state=NAS_TASK_FREE;
    ++released_children;
}
struct nas_files *nas_files_get(struct nas_files *f) {
    assert(f==&parent_files);return f;
}
void nas_files_put(struct nas_files *f) {assert(f==&child_files);}
struct nas_fs_context *nas_fs_context_get(struct nas_fs_context *f) {
    assert(f==&parent_fs);return f;
}
void nas_fs_context_put(struct nas_fs_context *f) {assert(f==&child_fs);}
struct nas_sighand *nas_sighand_get(struct nas_sighand *s) {
    assert(s==&parent_sighand);return s;
}
void nas_sighand_put(struct nas_sighand *s) {assert(s==&child_sighand);}
bool nas_user_address_writable(uint64_t addr) {
    return addr==0x7000 || addr==0x7ff8;
}
bool nas_user_write_bytes(uint64_t addr,const uint8_t *src,uint64_t len) {
    ++write_attempts;
    assert(len==4);
    last_written_address=addr;
    last_written_mm=(uint64_t)(uintptr_t)nas_current_task->mm;
    last_written_word=(uint32_t)src[0]|((uint32_t)src[1]<<8)|
                      ((uint32_t)src[2]<<16)|((uint32_t)src[3]<<24);
    if(reject_user_write || (addr==robust_node+8 && reject_robust_write) ||
       addr>=USER_LIMIT || addr==1)return false;
    if(addr==robust_node+8)robust_word=last_written_word;
    ++successful_writes;
    return true;
}
bool nas_user_read_bytes(uint64_t addr,uint8_t *dest,uint64_t len) {
    if(addr!=robust_node+8 || len!=4)return false;
    for(unsigned i=0;i<4;i++)dest[i]=(uint8_t)(robust_word>>(i*8));
    return true;
}
bool nas_user_read_u64(uint64_t addr,uint64_t *result) {
    if(addr==robust_head)*result=robust_next;
    else if(addr==robust_head+8)*result=8;
    else if(addr==robust_head+16)*result=robust_pending;
    else if(addr==robust_node)*result=robust_head;
    else return false;
    return true;
}
unsigned nas_futex_wait_wake(struct nas_mm *mm,uint64_t addr,
                             unsigned max,uint32_t mask) {
    assert(mm==nas_current_task->mm && max==1 && mask==UINT32_MAX);
    last_wake_address=addr;
    ++wakes;
    return 1;
}
static void setup_parent(void) {
    parent=(struct nas_task){0};
    parent.pid=1;
    parent.tgid=1;
    parent.state=NAS_TASK_RUNNING;
    parent.mm=&parent_mm;
    parent.files=&parent_files;
    parent.fds=&parent_files.table;
    parent.fs=&parent_fs;
    parent.sighand=&parent_sighand;
    parent.group_leader=&parent;
    parent.thread_set_tid=0x3000; /* adversarial inherited pre-dispatch state */
    parent.thread_clear_tid=0x3000;
    nas_current_task=&parent;
    write_attempts=successful_writes=wakes=0;
    last_written_mm=last_written_address=last_wake_address=0;
    last_parent_return=last_child_return=0;
    reject_user_write=reject_robust_write=false;
    robust_next=robust_node;
    robust_pending=0;
}
static void first_dispatch_and_exit(uint64_t addr,bool writable) {
    nas_current_task=&child;
    unsigned writes_before=write_attempts;
    nas_process_child_tid_first_dispatch();
    assert(write_attempts==writes_before+1);
    assert(last_written_address==addr);
    assert(last_written_word==(uint32_t)child.pid);
    assert(last_written_mm==(uint64_t)(uintptr_t)child.mm);
    assert(child.thread_set_tid==0);
    nas_process_child_tid_first_dispatch();
    assert(write_attempts==writes_before+1); /* one-shot, no stale repeat */
    if(writable)assert(successful_writes>0);
    unsigned wakes_before=wakes;
    nas_process_clear_tid_for_exit(&child);
    assert(child.thread_clear_tid==0 && wakes==wakes_before+1);
    assert(last_wake_address==addr && last_written_word==0);
    /* Repeated exit cleanup is idempotent, including futex wakeup count. */
    nas_process_clear_tid_for_exit(&child);
    assert(wakes==wakes_before+1);
}
static void clear_only_thread_exit(uint64_t addr) {
    nas_current_task=&child;
    unsigned writes_before=write_attempts;
    nas_process_child_tid_first_dispatch();
    assert(write_attempts==writes_before && child.thread_set_tid==0);
    nas_process_clear_tid_for_exit(&child);
    assert(write_attempts==writes_before+1 && child.thread_clear_tid==0);
    assert(last_written_address==addr && last_written_word==0);
    assert(last_wake_address==addr && wakes==1);
    nas_process_clear_tid_for_exit(&child);
    assert(wakes==1);
}
int main(void) {
    uint64_t frame[16]={0};
    setup_parent();
    unsigned allocations_before=allocated_children;
    assert(nas_process_fork_child_tid(frame,0x8000)==101);
    assert(allocated_children==allocations_before+1);
    assert(child.mm==&child_mm && child.thread_clear_tid==0x8000 &&
           child.thread_set_tid==0x8000);
    assert(write_attempts==0 && last_parent_return==101 && !last_child_return);
    first_dispatch_and_exit(0x8000,true);

    /* Linux CHILD_SETTID bad pointer is a child-side best-effort put_user.
     * Parent still receives the real PID; later clear failure still wakes. */
    setup_parent();
    assert(nas_process_fork_child_tid(frame,1)==102);
    assert(child.thread_clear_tid==1 && write_attempts==0);
    first_dispatch_and_exit(1,false);
    assert(successful_writes==0 && wakes==1);

    /* Bare fork is neither SET nor CLEAR. In particular it must never
     * retain the parent's copied but still pending set-child address. */
    setup_parent();
    assert(nas_process_fork(frame)==103);
    assert(child.thread_set_tid==0 && child.thread_clear_tid==0);
    nas_current_task=&child;
    nas_process_child_tid_first_dispatch();
    nas_process_clear_tid_for_exit(&child);
    assert(write_attempts==0 && wakes==0);

    setup_parent();
    assert(nas_process_clone_thread(frame,0x8000,0x9000,0xa000,
                                    0x2000,0x003d0f00ULL)==104);
    assert(child.mm==&parent_mm && child.is_thread && child.tgid==1);
    assert(child.files==&parent_files && child.fs==&parent_fs &&
           child.sighand==&parent_sighand);
    assert(child.thread_set_tid==0 && child.thread_clear_tid==0xa000);
    assert(write_attempts==1 && last_written_address==0x9000);
    clear_only_thread_exit(0xa000);
    assert(wakes==1 && last_wake_address==0xa000);

    /* Both TID pointers can fault independently. A child already created
     * must never be returned as EFAULT or accidentally released. */
    setup_parent();
    reject_user_write=true;
    assert(nas_process_clone_thread(frame,0x8000,1,1,
                                    0x2000,0x003d0f00ULL)==105);
    assert(child.state==NAS_TASK_READY && write_attempts==1);
    clear_only_thread_exit(1);
    assert(wakes==1 && panic_count==0 && released_children==0);

    /* Robust-list owner death happens before clear_child_tid, and the
     * unrelated futex waiters are woken precisely once on owner death. */
    setup_parent();
    child=(struct nas_task){0};
    child.pid=106;child.mm=&parent_mm;child.robust_list_head=robust_head;
    robust_word=UINT32_C(0x80000000)|106U;
    nas_current_task=&child;
    nas_process_robust_exit(&child);
    assert(robust_word==UINT32_C(0xc0000000));
    assert(child.robust_list_head==0 && wakes==1);
    assert(last_wake_address==robust_node+8);
    nas_process_robust_exit(&child);
    assert(wakes==1);

    setup_parent();
    child=(struct nas_task){0};
    child.pid=107;child.mm=&parent_mm;child.robust_list_head=robust_head;
    robust_word=UINT32_C(0x80000000)|107U;
    reject_robust_write=true;
    nas_current_task=&child;
    nas_process_robust_exit(&child);
    assert(robust_word==(UINT32_C(0x80000000)|107U));
    assert(child.robust_list_head==0 && wakes==0 && panic_count==0);
    /* Linux 2026 fix: a dying pending unlocker/woken waiter must pass
     * its one-shot wake to the next waiter when WAITERS was cleared.
     * A=unlocked, B=other owner, D=prior owner died; C already has
     * WAITERS set and must NOT get an unnecessary additional wake. */
    const uint32_t pending_states[]={0U,107U,UINT32_C(0x40000000),
                                     UINT32_C(0xc0000000),
                                     UINT32_C(0x80000000)|107U};
    for(unsigned i=0;i<5;i++) {
        setup_parent();
        child=(struct nas_task){0};
        child.pid=108;child.mm=&parent_mm;child.robust_list_head=robust_head;
        robust_next=robust_head;robust_pending=robust_node;
        robust_word=pending_states[i];
        nas_current_task=&child;
        nas_process_robust_exit(&child);
        assert(robust_word==pending_states[i] && child.robust_list_head==0);
        assert(wakes==(i==4 ? 0U : 1U));
        if(i!=4)assert(last_wake_address==robust_node+8);
        assert(panic_count==0);
    }
    /* Original Go age-keygen AArch64: 0x50f00 shares VM/FD/FS/SIGHAND
     * but does NOT request either TLS replacement or TID side effects.
     * A nonzero arg4/parent_tid must be ignored without touching memory. */
    setup_parent();
    nas_arch_context_set_tls(&parent.arch,0x3403a090ULL);
    assert(nas_process_clone_thread(frame,0x8000,0x34002540,0x3402c020,
                                    UINT64_MAX,0x00050f00ULL)==106);
    assert(child.is_thread && child.mm==&parent_mm);
    assert(nas_arch_context_tls_base(&child.arch)==0x3403a090ULL);
    assert(child.thread_set_tid==0 && child.thread_clear_tid==0);
    assert(write_attempts==0 && last_parent_return==106);
    nas_current_task=&child;
    nas_process_child_tid_first_dispatch();
    nas_process_clear_tid_for_exit(&child);
    assert(write_attempts==0 && wakes==0);

    /* Original Go x86: 0xd0f00 includes SETTLS but no parent-/child-TID
     * side effects, even when incidental raw argument slots are nonzero. */
    setup_parent();
    nas_arch_context_set_tls(&parent.arch,0x33000000ULL);
    assert(nas_process_clone_thread(frame,0x8000,0x34002540,0x3402c020,
                                    0x3403a090,0x000d0f00ULL)==107);
    assert(nas_arch_context_tls_base(&child.arch)==0x3403a090ULL);
    assert(child.thread_set_tid==0 && child.thread_clear_tid==0);
    assert(write_attempts==0 && last_parent_return==107);
    nas_current_task=&child;
    nas_process_child_tid_first_dispatch();
    nas_process_clear_tid_for_exit(&child);
    assert(write_attempts==0 && wakes==0);
    printf("[M103 TASK PASS] deferred CHILD_SETTID, bad TID put_user, clear+wake, shared clone, robust owner-death, no panic (%u clones)\n",allocated_children);
    return 0;
}
