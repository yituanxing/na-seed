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
#include "../time/clock.h"
#include "sched.h"
extern void nas_print(const char *);
extern void nas_dec(uint64_t);
extern void nas_hex(uint64_t);
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
    /* This path changes the child SP. Unlike fork with an inherited stack,
     * the live trap frame must be replaced, not only the saved Task copy. */
    nas_arch_load_user_frame(child, frame);
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
/* Linux rlimit64: report resource limits the kernel actually enforces.
 * A limit update is NOT a no-op success: reject until the underlying
 * resource scheduler/FD/VM policy can enforce mutable soft/hard values. */
uint64_t nas_process_prlimit(uint64_t pid,uint64_t resource,
                             uint64_t new_addr,uint64_t old_addr) {
    if (pid && pid != nas_current_task->pid)
        return nas_task_by_pid(pid) ? (uint64_t)-EPERM : (uint64_t)-ESRCH;
    if (resource > 15) return (uint64_t)-EINVAL;
    if (new_addr) return (uint64_t)-EOPNOTSUPP;
    uint64_t soft=UINT64_MAX, hard=UINT64_MAX;
    switch (resource) {
    case 3: soft=hard=32ULL*4096ULL; break; /* RLIMIT_STACK */
    case 4: soft=hard=0; break;             /* RLIMIT_CORE: no dumps */
    case 6: soft=hard=NAS_TASK_LIMIT; break; /* RLIMIT_NPROC */
    case 7: soft=hard=NAS_FD_LIMIT; break; /* RLIMIT_NOFILE */
    case 8: soft=hard=0; break;             /* RLIMIT_MEMLOCK */
    case 13: case 14: soft=hard=0; break;   /* NICE/RTPRIO */
    default: break; /* No separate resource-limit policy. */
    }
    if (old_addr) {
        const uint64_t pair[2]={soft,hard};
        if (!nas_user_write_bytes(old_addr,(const uint8_t *)pair,sizeof(pair)))
            return (uint64_t)-EFAULT;
    }
    return 0;
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
    (void)nas_process_futex_wake(tid_addr,1,UINT32_MAX);
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


/* A bounded single-CPU Linux thread group: only musl's exact pthread clone
 * flag set is accepted by the ISA decoder. The VM and FD table are truly
 * shared, while user stack, TLS, TID and trap/FPU registers are task-local.
 * No clone of another ISA backend or special application code exists. */
uint64_t nas_process_clone_thread(uint64_t *frame,uint64_t stack,
                                 uint64_t parent_tid,uint64_t child_tid,
                                 uint64_t tls) {
    struct nas_task *parent=nas_current_task;
    if (!stack || stack>=USER_LIMIT || tls>=USER_LIMIT ||
        !parent_tid || !child_tid || parent_tid>USER_LIMIT-4 ||
        child_tid>USER_LIMIT-4 || stack<16 ||
        !nas_mmu_user_writable(stack-8) ||
        !nas_mmu_user_writable(parent_tid) ||
        !nas_mmu_user_writable(parent_tid+3) ||
        !nas_mmu_user_writable(child_tid) ||
        !nas_mmu_user_writable(child_tid+3))return (uint64_t)-EFAULT;
    nas_arch_save_user_frame(parent,frame);
    nas_arch_save_fpu(parent);
    struct nas_task *child=nas_task_fork_slot();
    if (!child)return (uint64_t)-EAGAIN;
    child->tgid=parent->tgid;
    child->is_thread=true;
    child->ppid=parent->ppid;
    child->vm_owner=parent->vm_owner;
    child->fds=parent->fds; /* CLONE_FILES: do NOT increase description refs */
    nas_task_set_root(child,parent->root_phys);
    nas_arch_context_set_user_sp(&child->arch,stack);
    nas_arch_context_set_tls(&child->arch,tls);
    child->thread_clear_tid=child_tid;
    uint32_t tid=(uint32_t)child->pid;
    uint8_t value[4]={(uint8_t)tid,(uint8_t)(tid>>8),
                      (uint8_t)(tid>>16),(uint8_t)(tid>>24)};
    if (!nas_user_write_bytes(parent_tid,value,sizeof(value)) ||
        !nas_user_write_bytes(child_tid,value,sizeof(value)))
        proc_fail("thread TID store failed after validation");
    nas_arch_set_saved_return(parent,child->pid);
    parent->resume_valid=true;
    parent->pending_valid=false;
    parent->blocked_op=0;
    parent->state=NAS_TASK_READY;
    child->state=NAS_TASK_RUNNING;
    nas_current_task=child;
    nas_arch_restore_cpu(child);
    nas_arch_load_user_frame(child,frame);
    nas_print("[THREAD] clone tid=");nas_dec(child->pid);
    nas_print(" tgid=");nas_dec(child->tgid);nas_print("\n");
    return 0;
}
#ifdef NASEED_SYSCALL_TRACE
static unsigned futex_trace_count;
static void futex_debug(const char *act,uint64_t addr,uint64_t val,uint64_t ret) {
    if(futex_trace_count++>700)return;
    nas_print("[FUTEX] "); nas_print(act);
    nas_print(" tid=");nas_dec(nas_current_task->pid);
    nas_print(" addr=");nas_hex(addr);
    nas_print(" val=");nas_hex(val);
    nas_print(" result=");nas_dec(ret);
    nas_print("\n");
}
#else
#define futex_debug(a,b,c,d) ((void)0)
#endif
unsigned nas_process_futex_wake(uint64_t uaddr,unsigned max,uint32_t mask) {
    unsigned count=0;
    for(unsigned i=0;i<NAS_TASK_LIMIT && count<max;i++) {
        struct nas_task *t=nas_task_at(i);
        if(t->state!=NAS_TASK_BLOCKED || !t->pending_valid ||
           t->blocked_op!=NAS_SC_futex || t->futex_addr!=uaddr ||
           t->vm_owner!=nas_current_task->vm_owner ||
           t->futex_woken || !(t->futex_mask&mask))continue;
        t->futex_woken=true;
        count++;
    }
    futex_debug("wake",uaddr,max,count);
    return count;
}
/* Linux futex word ABI is a native aligned 32-bit user value, on each ISA.
 * WAIT and WAIT_BITSET are truly blocking in shared single-CPU scheduler.
 * Unsupported PI/requeue/robust operations return ENOSYS, never success. */
uint64_t nas_process_futex(uint64_t *frame,uint64_t addr,uint64_t op,
                           uint64_t val,uint64_t timeout,uint64_t addr2,
                           uint64_t val3) {
    (void)addr2;
    if ((addr&3U) || addr>USER_LIMIT-4)return (uint64_t)-EINVAL;
    uint64_t command=op&127U;
    if(op & ~(uint64_t)(127U|128U|256U))return (uint64_t)-EINVAL;
    if((op&256U) && command!=9U)return (uint64_t)-ENOSYS;
    if(command==1U || command==10U) {
        uint32_t mask=command==10U?(uint32_t)val3:UINT32_MAX;
        if(!mask)return (uint64_t)-EINVAL;
        if(!nas_mmu_user_byte(addr) || !nas_mmu_user_byte(addr+3))
            return (uint64_t)-EFAULT;
        unsigned n=val>NAS_TASK_LIMIT?NAS_TASK_LIMIT:(unsigned)val;
        return nas_process_futex_wake(addr,n,mask);
    }
    if(command!=0U && command!=9U)return (uint64_t)-ENOSYS;
    uint32_t mask=command==9U?(uint32_t)val3:UINT32_MAX;
    if(!mask)return (uint64_t)-EINVAL;
    uint8_t bytes[4];
    if(!nas_user_read_bytes(addr,bytes,4))return (uint64_t)-EFAULT;
    uint32_t word=(uint32_t)bytes[0]|((uint32_t)bytes[1]<<8)|
                  ((uint32_t)bytes[2]<<16)|((uint32_t)bytes[3]<<24);
    if(word!=(uint32_t)val) {futex_debug("mismatch",addr,val,word);return (uint64_t)-EAGAIN;}
    futex_debug("wait",addr,val,op);
    nas_current_task->deadline_active=false;
    if(timeout) {
        uint64_t sec,nsec,ns;
        if(timeout>USER_LIMIT-16 ||
           !nas_user_read_u64(timeout,&sec) ||
           !nas_user_read_u64(timeout+8,&nsec))
            return (uint64_t)-EFAULT;
        if(!nas_time_duration_ns(sec,nsec,&ns))return (uint64_t)-EINVAL;
        uint64_t now=nas_arch_monotonic_ns();
        if(command==9U) { /* absolute CLOCK_MONOTONIC / REALTIME */
            if(op&256U) {
                uint64_t epoch;
                if(!nas_time_realtime_ns(&epoch))return (uint64_t)-EOPNOTSUPP;
                ns=ns>epoch?ns-epoch:0;
                ns=nas_time_add_deadline(now,ns);
            }
            if(ns<=now)return (uint64_t)-ETIMEDOUT;
        }else{
            if(!ns)return (uint64_t)-ETIMEDOUT;
            ns=nas_time_add_deadline(now,ns);
        }
        nas_current_task->deadline_ns=ns;
        nas_current_task->deadline_active=true;
    }
    nas_current_task->futex_addr=addr;
    nas_current_task->futex_mask=mask;
    nas_current_task->futex_woken=false;
    return nas_task_block(frame,NAS_SC_futex,addr,op,val,timeout);
}
