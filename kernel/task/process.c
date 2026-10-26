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
/* Linux task name is exactly 16 bytes including its NUL terminator.
 * Other prctl options must not quietly report success: they have separate
 * process-control semantics which are not yet implemented. */
uint64_t nas_process_prctl(uint64_t option,uint64_t arg) {
    struct nas_task *task=nas_current_task;
    if(option==15ULL) { /* PR_SET_NAME */
        char name[16]={0};
        for(unsigned i=0;i<15;i++) {
            uint8_t c;
            if(!nas_user_read_bytes(arg+i,&c,1))return (uint64_t)-EFAULT;
            name[i]=(char)c;
            if(!c)break;
        }
        for(unsigned i=0;i<16;i++)task->comm[i]=name[i];
        return 0;
    }
    if(option==16ULL) /* PR_GET_NAME */
        return nas_user_write_bytes(arg,(const uint8_t *)task->comm,16)
            ? 0:(uint64_t)-EFAULT;
    return (uint64_t)-EINVAL;
}
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
    parent->wait.blocked_op = 0;
    parent->wait.pending_valid = false;
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
    parent->wait.pending_valid = false;
    parent->wait.blocked_op = 0;
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
        !parent->resume_valid || parent->wait.pending_valid)
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
    /* All current NaSeed userspace tasks share the one real root UID.
     * The enforced resource limits below are kernel-wide constants:
     * read-only prlimit64 of an existing external PID exposes no private
     * mutable state. Linux permits this same-credential observation.
     * Once UID/ptrace access policy exists, centralize the new check there,
     * not in per-program syscall special cases. */
    if (pid && (pid>UINT32_MAX || !nas_task_by_pid(pid)))
        return (uint64_t)-ESRCH;
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

/* Linux64 non-PI robust-futex linked-list ABI. Linux's own kernel
 * documents these 3 native unsigned-long fields (list.next,
 * signed futex_offset, list_op_pending). Userspace owns the list.
 * No per-lock registration; no new ISA-specific implementation.
 *
 * Exit reads the dying thread's list inside the ACTIVE shared VM.
 * Never trust user-provided pointers, offsets or cyclic link chains.
 * This stage supports non-PI owner death; PI futex locking remains ENOSYS. */
#define NAS_ROBUST_HEAD_BYTES 24U
#define NAS_ROBUST_OWNER_MASK 0x3fffffffU
#define NAS_ROBUST_WAITERS 0x80000000U
#define NAS_ROBUST_OWNER_DIED 0x40000000U
#define NAS_ROBUST_WALK_LIMIT 2048U
uint64_t nas_process_set_robust_list(uint64_t head,uint64_t len) {
    if(len != NAS_ROBUST_HEAD_BYTES)return (uint64_t)-EINVAL;
    /* Linux stores a user pointer and validates the list when exiting.
     * NULL unregisters the robust list for this thread. */
    if(head>=USER_LIMIT && head)return (uint64_t)-EFAULT;
    nas_current_task->robust_list_head=head;
    return 0;
}
uint64_t nas_process_get_robust_list(uint64_t pid,uint64_t head_addr,
                                     uint64_t len_addr) {
    struct nas_task *task;
    if(!pid) task=nas_current_task;
    else {
        if(pid>UINT32_MAX)return (uint64_t)-ESRCH;
        task=nas_task_by_pid(pid);
        if(!task)return (uint64_t)-ESRCH;
        /* Only within-thread-group introspection is currently proven.
         * Never expose another process's pointer with a fake ptrace check. */
        if(task->tgid!=nas_current_task->tgid)return (uint64_t)-EPERM;
    }
    if(!head_addr || !len_addr || head_addr>USER_LIMIT-8 ||
       len_addr>USER_LIMIT-8 ||
       !nas_mmu_user_writable(head_addr) ||
       !nas_mmu_user_writable(head_addr+7) ||
       !nas_mmu_user_writable(len_addr) ||
       !nas_mmu_user_writable(len_addr+7))
        return (uint64_t)-EFAULT;
    if(!nas_user_write_u64(head_addr,task->robust_list_head) ||
       !nas_user_write_u64(len_addr,NAS_ROBUST_HEAD_BYTES))
        return (uint64_t)-EFAULT;
    return 0;
}
/* A raw node points at a robust_list.next field, NOT the futex word.
 * A signed futex_offset may be negative (musl/glibc mutex layout).
 * The low bit of a list link flags an unsupported PI entry: it must not
 * be mistaken for a regular futex, nor permit invalid memory access. */
static void robust_mark_owner_died(struct nas_task *thread,uint64_t node,
                                    int64_t offset,bool is_pi) {
    if(is_pi || !node)return;
    uint64_t addr=node+(uint64_t)offset;
    if((offset>=0 && addr<node) || (offset<0 && addr>node) ||
       (addr&3U) || addr>USER_LIMIT-4)return;
    uint8_t word_bytes[4];
    if(!nas_user_read_bytes(addr,word_bytes,4))return;
    uint32_t word=(uint32_t)word_bytes[0] |
                 ((uint32_t)word_bytes[1]<<8) |
                 ((uint32_t)word_bytes[2]<<16) |
                 ((uint32_t)word_bytes[3]<<24);
    if((word&NAS_ROBUST_OWNER_MASK)!=(uint32_t)thread->pid)return;
    uint32_t changed=(word&NAS_ROBUST_WAITERS)|NAS_ROBUST_OWNER_DIED;
    uint8_t updated[4]={(uint8_t)changed,(uint8_t)(changed>>8),
                        (uint8_t)(changed>>16),(uint8_t)(changed>>24)};
    if(!nas_user_write_bytes(addr,updated,4))return;
    if(word&NAS_ROBUST_WAITERS)
        (void)nas_process_futex_wake(addr,1,UINT32_MAX);
}
void nas_process_robust_exit(struct nas_task *thread) {
    if(!thread || !thread->robust_list_head)return;
    if(thread->vm_owner!=nas_current_task->vm_owner)
        proc_fail("robust list exit from unrelated page table");
    uint64_t head=thread->robust_list_head;
    thread->robust_list_head=0; /* never walk twice after deferred leader exit */
    uint64_t next=0,offbits=0,pending=0;
    if(head>USER_LIMIT-NAS_ROBUST_HEAD_BYTES ||
       !nas_user_read_u64(head,&next) ||
       !nas_user_read_u64(head+8,&offbits) ||
       !nas_user_read_u64(head+16,&pending))return;
    int64_t offset=(int64_t)offbits;
    uint64_t cursor=next;
    for(unsigned i=0;i<NAS_ROBUST_WALK_LIMIT;i++) {
        uint64_t node=cursor&~1ULL;
        if(!node || node==head || node>USER_LIMIT-8)break;
        uint64_t following=0;
        if(!nas_user_read_u64(node,&following))break;
        if(node!=(pending&~1ULL))
            robust_mark_owner_died(thread,node,offset,(cursor&1U)!=0);
        cursor=following;
    }
    if(pending)
        robust_mark_owner_died(thread,pending&~1ULL,offset,(pending&1U)!=0);
}
void nas_process_clear_tid_for_exit(struct nas_task *thread) {
    if (!thread || !thread->thread_clear_tid) return;
    if (thread->vm_owner != nas_current_task->vm_owner)
        proc_fail("cross-VM clear_child_tid on thread-group exit");
    uint64_t tid_addr=thread->thread_clear_tid;
    /* Linux CLONE_CHILD_CLEARTID stores zero before FUTEX_WAKE in the
     * exiting group's shared userspace VM, including remote dying workers. */
    static const uint8_t zero[4]={0,0,0,0};
    if (!nas_user_write_bytes(tid_addr,zero,sizeof(zero)))
        proc_fail("clone clear_child_tid unmapped before exit");
    thread->thread_clear_tid=0;
    (void)nas_process_futex_wake(tid_addr,1,UINT32_MAX);
}
/* execve succeeds atomically at the old-VM cleanup boundary. Threads
 * that did not request exec cease to exist; unlike exit_group, we DO
 * NOT signal the parent, close inherited files or create wait4 zombies.
 * Robust owner death and clear_child_tid run while the OLD root remains
 * installed. A replaced nonleader adopts the old TGID in exec.c. */
void nas_process_exec_quiesce_siblings(void) {
    struct nas_task *caller=nas_current_task;
    struct nas_task *leader=caller->vm_owner;
    if(!leader || leader->tgid!=caller->tgid || leader->is_thread)
        proc_fail("execve thread-group owner invalid");
    for(unsigned i=0;i<NAS_TASK_LIMIT;i++) {
        struct nas_task *peer=nas_task_at(i);
        if(peer==caller || peer==leader ||
           peer->state==NAS_TASK_FREE || peer->tgid!=leader->tgid)
            continue;
        if(!peer->is_thread)proc_fail("execve crossed process boundary");
        nas_process_robust_exit(peer);
        nas_process_clear_tid_for_exit(peer);
        peer->state=NAS_TASK_ZOMBIE;
        peer->resume_valid=false;
        peer->wait.pending_valid=false;
        peer->wait.blocked_op=0;
        peer->wait.futex_addr=0;
        nas_task_release(peer);
    }
    if(caller!=leader) {
        /* The original leader is replaced by this worker. It is NEVER
         * exposed as a zombie to parent wait4 during execve. */
        nas_process_robust_exit(leader);
        nas_process_clear_tid_for_exit(leader);
    }
}
void nas_process_clear_tid_on_exit(void) {
    nas_process_clear_tid_for_exit(nas_current_task);
}

/* Shared wait readiness used by both synchronous wait4 and the scheduler
 * blocked-syscall wake predicate. Stop/continue notifications are one-shot;
 * exit zombies take priority and are reclaimed only on successful wait4.
 */
bool nas_process_waitable(const struct nas_task *child, uint64_t options) {
    if (child->state == NAS_TASK_ZOMBIE) return !child->group_exit_pending;
    if (((options & NAS_WAIT_UNTRACED) || child->ptrace_tracer_pid) &&
        child->state == NAS_TASK_STOPPED && child->stop_report_pending)
        return true;
    if ((options & NAS_WAIT_CONTINUED) && child->continue_report_pending)
        return true;
    return false;
}
uint64_t nas_process_wait4(uint64_t desired, uint64_t status_addr,
                           uint64_t options, uint64_t rusage) {
    if (options & ~NAS_WAIT_SUPPORTED)
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
                       ((options & NAS_WAIT_UNTRACED) || child->ptrace_tracer_pid);
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
    parent->wait.pending_valid=false;
    parent->wait.blocked_op=0;
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
        if(t->state!=NAS_TASK_BLOCKED || !t->wait.pending_valid ||
           t->wait.blocked_op!=NAS_SC_futex || t->wait.futex_addr!=uaddr ||
           t->vm_owner!=nas_current_task->vm_owner ||
           t->wait.futex_woken || !(t->wait.futex_mask&mask))continue;
        t->wait.futex_woken=true;
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
    if ((addr&3U) || addr>USER_LIMIT-4)return (uint64_t)-EINVAL;
    uint64_t command=op&127U;
    if(op & ~(uint64_t)(127U|128U|256U))return (uint64_t)-EINVAL;
    if((op&256U) && command!=9U)return (uint64_t)-ENOSYS;
    /* Linux FUTEX_REQUEUE / FUTEX_CMP_REQUEUE: argument 4 is a COUNT,
     * NOT a userspace timespec. musl's pthread condvar release uses
     * FUTEX_REQUEUE|PRIVATE with (wake=0,requeue=1,target=mutex).
     * A woken waiter completes its original WAIT with success; a
     * transferred waiter stays parked until the target mutex wakes it.
     * Non-PI queue transfer is process/VM scoped, not an invented wake. */
    if(command==3U || command==4U) {
        if((addr2&3U) || addr2>USER_LIMIT-4)return (uint64_t)-EINVAL;
        if(!nas_mmu_user_byte(addr) || !nas_mmu_user_byte(addr+3) ||
           !nas_mmu_user_byte(addr2) || !nas_mmu_user_byte(addr2+3))
            return (uint64_t)-EFAULT;
        if(command==4U) {
            uint8_t current[4];
            if(!nas_user_read_bytes(addr,current,4))return (uint64_t)-EFAULT;
            uint32_t word=(uint32_t)current[0]|((uint32_t)current[1]<<8)|
                          ((uint32_t)current[2]<<16)|((uint32_t)current[3]<<24);
            if(word!=(uint32_t)val3)return (uint64_t)-EAGAIN;
        }
        unsigned nwake=val>NAS_TASK_LIMIT?NAS_TASK_LIMIT:(unsigned)val;
        unsigned nmove=timeout>NAS_TASK_LIMIT?NAS_TASK_LIMIT:(unsigned)timeout;
        unsigned woke=nas_process_futex_wake(addr,nwake,UINT32_MAX);
        unsigned moved=0;
        for(unsigned i=0;i<NAS_TASK_LIMIT && moved<nmove;i++) {
            struct nas_task *t=nas_task_at(i);
            if(t->state!=NAS_TASK_BLOCKED || !t->wait.pending_valid ||
               t->wait.blocked_op!=NAS_SC_futex || t->wait.futex_addr!=addr ||
               t->vm_owner!=nas_current_task->vm_owner || t->wait.futex_woken)
                continue;
            t->wait.futex_addr=addr2;
            moved++;
        }
        futex_debug("requeue",addr,addr2,((uint64_t)woke<<32)|moved);
        return woke+moved;
    }
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
    nas_current_task->wait.deadline_active=false;
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
        nas_current_task->wait.deadline_ns=ns;
        nas_current_task->wait.deadline_active=true;
    }
    nas_current_task->wait.futex_addr=addr;
    nas_current_task->wait.futex_mask=mask;
    nas_current_task->wait.futex_woken=false;
    return nas_task_block(frame,NAS_SC_futex,addr,op,val,timeout);
}
