#include <stdint.h>
#include <stdbool.h>
#include "../errno.h"
#include "../task.h"
#include "../arch/arch.h"
#include "../arch/mmu.h"
#include "../fd.h"
#include "../mm/usercopy.h"
#include "process.h"
#include "wait_pid.h"
extern void nas_print(const char *);
extern void nas_dec(uint64_t);
static void proc_fail(const char *s) __attribute__((noreturn));
static void proc_fail(const char *s) { nas_print("[FAIL] M1 "); nas_print(s); nas_print("\n"); nas_arch_halt(); }
/* A cooperative child-first fork; parent remains READY while child runs.
 * Once a child blocks on a pipe, another READY task runs and the blocked
 * syscall is completed later in its owner's address space. No timer
 * preemption, SMP, or independent kernel stack is claimed.
 */
/* One architecture-neutral process fork algorithm, with optional Linux
 * per-child TID setup. The original shared VM and scheduler algorithms
 * remain in charge; no RV-specific fork implementation is introduced. */
static uint64_t fork_common(uint64_t *frame, uint64_t child_tid) {
    /* Validate before allocating task/VM objects. Linux CHILD_SETTID
     * stores a native 32-bit PID in the CHILD's copied address space, not
     * in the parent. Check both ends in case of a page boundary. */
    if (child_tid &&
        (child_tid > UINT64_MAX-3 ||
         !nas_mmu_user_writable(child_tid) ||
         !nas_mmu_user_writable(child_tid+3)))
        return (uint64_t)-EFAULT;
    struct nas_task *parent = nas_current_task;
    /* The child slot is copied from the parent, including its ARCH context.
     * Save LIVE user GPR/PC/SP/TLS and FPU BEFORE that copy, otherwise a
     * freshly booted task may still have a zero saved thread pointer.
     * This is critical on RV64: libc uses tp-relative stores immediately
     * after clone and zeroing tp causes a real U-mode store page fault.
     * This ordering is common to all architectures, not an RV workaround. */
    nas_arch_save_user_frame(parent, frame);
    nas_arch_save_fpu(parent);
    struct nas_task *child = nas_task_fork_slot();
    if (!child) return (uint64_t)-EAGAIN;
    uint64_t new_root = nas_mmu_fork_root();
    if (!new_root) {
        nas_task_release(child);
        return (uint64_t)-ENOMEM;
    }
    nas_task_set_root(child, new_root);
    if (!nas_task_hold_files(child)) proc_fail("fork dangling open-file slot");
    nas_arch_set_saved_return(parent, child->pid);
    parent->resume_valid = true;
    parent->blocked_op = 0;
    parent->pending_valid = false;
    parent->state = NAS_TASK_READY;
    child->state = NAS_TASK_RUNNING;
    nas_current_task = child;
    nas_arch_restore_cpu(child);
    if (child_tid) {
        /* Entire 4-byte write uses the child CR3/satp copy. Parent's
         * memory must retain its independent old contents. */
        uint32_t tid=(uint32_t)child->pid;
        uint8_t bytes[4]={(uint8_t)tid,(uint8_t)(tid>>8),
                          (uint8_t)(tid>>16),(uint8_t)(tid>>24)};
        if (!nas_user_write_bytes(child_tid,bytes,sizeof(bytes)))
            proc_fail("clone child_tid write failed after writable validation");
        child->thread_clear_tid=child_tid;
        nas_print("[PROC] clone child TID initialized pid=");
        nas_dec(child->pid);
        nas_print("\n");
    }
    nas_print("[PROC] fork parent="); nas_dec(parent->pid);
    nas_print(" child="); nas_dec(child->pid); nas_print("\n");
    return 0; /* shared syscall decoder sees zero in new child */
}
uint64_t nas_process_fork(uint64_t *frame) {
    return fork_common(frame,0);
}
uint64_t nas_process_fork_child_tid(uint64_t *frame,uint64_t child_tid) {
    if(!child_tid) return (uint64_t)-EFAULT;
    return fork_common(frame,child_tid);
}
/* Real shared root for CLONE_VM, parent suspended for CLONE_VFORK.
 * The parent retains page-table ownership; the child never reclaims it. */
uint64_t nas_process_vfork(uint64_t *frame, uint64_t child_stack) {
    struct nas_task *parent = nas_current_task;
    if (parent->vfork_child_pid) return (uint64_t)-EAGAIN;
    if (child_stack &&
        (child_stack >= USER_LIMIT || child_stack < 16 ||
         !nas_mmu_user_writable(child_stack-1) ||
         !nas_mmu_user_writable(child_stack-8)))
        return (uint64_t)-EFAULT;
    nas_arch_save_user_frame(parent, frame);
    nas_arch_save_fpu(parent);
    struct nas_task *child = nas_task_fork_slot();
    if (!child) return (uint64_t)-EAGAIN;
    nas_task_set_root(child, parent->root_phys);
    child->vfork_parent_pid = parent->pid;
    child->vfork_shared_vm = true;
    if (child_stack)
        nas_arch_context_set_user_sp(&child->arch, child_stack);
    if (!nas_task_hold_files(child))
        proc_fail("vfork dangling file description");
    nas_arch_set_saved_return(parent, child->pid);
    parent->vfork_child_pid = child->pid;
    parent->resume_valid = true;
    parent->pending_valid = false;
    parent->blocked_op = 0;
    parent->state = NAS_TASK_BLOCKED;
    child->state = NAS_TASK_RUNNING;
    nas_current_task = child;
    nas_arch_restore_cpu(child);
    nas_print("[PROC] vfork parent="); nas_dec(parent->pid);
    nas_print(" child="); nas_dec(child->pid);
    nas_print(" shared VM\n");
    return 0;
}
void nas_process_vfork_release(void) {
    struct nas_task *child = nas_current_task;
    if (!child->vfork_parent_pid) return;
    struct nas_task *parent = nas_task_by_pid(child->vfork_parent_pid);
    if (!parent || parent->vfork_child_pid != child->pid ||
        parent->state != NAS_TASK_BLOCKED ||
        !parent->resume_valid || parent->pending_valid)
        proc_fail("vfork parent ownership corrupted");
    parent->vfork_child_pid = 0;
    parent->state = NAS_TASK_READY;
    child->vfork_parent_pid = 0;
    child->vfork_shared_vm = false;
    nas_print("[PROC] vfork parent resumed pid="); nas_dec(parent->pid);
    nas_print("\n");
}
void nas_process_clear_tid_on_exit(void) {
    uint64_t tid_addr=nas_current_task->thread_clear_tid;
    if (!tid_addr) return;
    /* For a forked process (separate VM), Linux clear_child_tid applies
     * only to the child's VM. Wake-on-futex only matters for shared-VM
     * threads, which this kernel does not claim to support. */
    static const uint8_t zero[4]={0,0,0,0};
    if (!nas_user_write_bytes(tid_addr,zero,sizeof(zero)))
        proc_fail("clone clear_child_tid unmapped before exit");
    nas_current_task->thread_clear_tid=0;
}

/* Shared wait readiness used by both synchronous wait4 and the scheduler
 * blocked-syscall wake predicate. Stop/continue notifications are one-shot;
 * exit zombies take priority and are reclaimed only on successful wait4.
 */
bool nas_process_waitable(const struct nas_task *child, uint64_t options) {
    if (child->state == NAS_TASK_ZOMBIE) return true;
    if ((options & NAS_WAIT_UNTRACED) &&
        child->state == NAS_TASK_STOPPED && child->stop_report_pending)
        return true;
    if ((options & NAS_WAIT_CONTINUED) && child->continue_report_pending)
        return true;
    return false;
}
uint64_t nas_process_wait4(uint64_t desired, uint64_t status_addr,
                           uint64_t options, uint64_t rusage) {
    if (options & ~(NAS_WAIT_NOHANG|NAS_WAIT_UNTRACED|NAS_WAIT_CONTINUED))
        return (uint64_t)-EINVAL;
    if (rusage) return (uint64_t)-EOPNOTSUPP;
    bool have_child = false;
    for (unsigned slot=0;slot<NAS_TASK_LIMIT;slot++) {
        struct nas_task *child = nas_task_child_at(nas_current_task->pid,slot);
        if (!child) continue;
        uint64_t pid=child->pid;
        if (!nas_wait_pid_matches(desired,pid,
                                  nas_current_task->pgid,child->pgid))
            continue;
        have_child=true;
        if (!nas_process_waitable(child,options)) continue;
        bool zombie = child->state == NAS_TASK_ZOMBIE;
        bool stopped = !zombie && child->state == NAS_TASK_STOPPED &&
                       child->stop_report_pending &&
                       (options & NAS_WAIT_UNTRACED);
        uint32_t code;
        if (zombie) {
            code = child->termination_signal ?
                child->termination_signal : (child->exit_code & 255U)<<8;
        } else if (stopped) {
            code = ((child->stop_signal ? child->stop_signal : 19U)<<8)|0x7fU;
        } else {
            code = 0xffffU; /* WIFCONTINUED */
        }
        if (status_addr) {
            uint8_t bytes[4] = {(uint8_t)code,(uint8_t)(code>>8),
                                (uint8_t)(code>>16),(uint8_t)(code>>24)};
            if (!nas_user_write_bytes(status_addr,bytes,4))
                return (uint64_t)-EFAULT; /* retain event on bad user buf */
        }
        if (zombie) {
            uint64_t owned_root = nas_task_page_root(child);
            if (owned_root && !nas_mmu_reclaim_root(owned_root))
                proc_fail("wait4 failed to reclaim child address space");
            nas_print("[PROC] wait4 reaped pid="); nas_dec(pid);
            nas_print(" status="); nas_dec(code); nas_print("\n");
            nas_task_release(child);
        } else if (stopped) {
            child->stop_report_pending=false;
        } else {
            child->continue_report_pending=false;
        }
        return pid;
    }
    return have_child
        ? ((options & NAS_WAIT_NOHANG) ? 0 : (uint64_t)-EOPNOTSUPP)
        : (uint64_t)-ECHILD;
}
