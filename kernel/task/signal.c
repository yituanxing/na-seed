#include <stdint.h>
#include "../errno.h"
#include "../task.h"
#include "../mm/usercopy.h"
#include "signal.h"
#include "../arch/arch.h"
#include "../task/sched.h"
/* Proper syscall-level rt_sigaction / sigprocmask structures, 8-byte sigset.
 * Delivery is intentionally not claimed until an actual signal path exists. */
uint64_t nas_signal_action(uint64_t signum, uint64_t new_action,
                              uint64_t old_action, uint64_t size) {
    if (size != 8 || signum < 1 || signum > 64) return (uint64_t)-EINVAL;
    if (new_action && (signum == 9 || signum == 19)) return (uint64_t)-EINVAL;
    if (old_action &&
        !nas_user_write_bytes(old_action, (const uint8_t *)nas_current_task->signal_actions[signum], 32))
        return (uint64_t)-EFAULT;
    if (new_action &&
        !nas_user_read_bytes(new_action, (uint8_t *)nas_current_task->signal_actions[signum], 32))
        return (uint64_t)-EFAULT;
    return 0;
}
uint64_t nas_signal_procmask(uint64_t how, uint64_t set,
                                uint64_t previous, uint64_t size) {
    if (size != 8) return (uint64_t)-EINVAL;
    uint64_t newmask = 0;
    if (set && !nas_user_read_u64(set, &newmask)) return (uint64_t)-EFAULT;
    if (previous && !nas_user_write_u64(previous, nas_current_task->blocked_signals)) return (uint64_t)-EFAULT;
    if (set) {
        if (how == 0) nas_current_task->blocked_signals |= newmask;        /* SIG_BLOCK */
        else if (how == 1) nas_current_task->blocked_signals &= ~newmask;  /* SIG_UNBLOCK */
        else if (how == 2) nas_current_task->blocked_signals = newmask;    /* SIG_SETMASK */
        else return (uint64_t)-EINVAL;
        nas_current_task->blocked_signals &= ~((1ULL << 8) | (1ULL << 18));
    }
    return 0;
}

/* Linux pid_t is signed32 even in LP64 x86, RV64, and ARM64.
 * This milestone supports signal-zero and unconditional SIGKILL. Unknown
 * signal-delivery semantics cannot return success without a signal frame. */
static bool signal_target_matches(const struct nas_task *target, int64_t pid,
                                   const struct nas_task *sender) {
    if (!target || target->state == NAS_TASK_FREE) return false;
    if (pid > 0) return target->pid == (uint64_t)pid;
    if (pid == 0) return target->pgid == sender->pgid;
    if (pid == -1) return target != sender && target->pid != 1;
    return target->pgid == (uint64_t)-pid;
}
/* Common cooperative process exit owns fd and pipe last-close, PID1
 * adoption, child TID clear and zombie state. On remote kill, the
 * target's VM MUST be active when clearing child_tid user memory.
 * The active caller's FPU, MMU and TLS are restored before continuing.
 * No kernel task stacks or architecture-specific process tables added. */
static void signal_kill_other(struct nas_task *target) {
    struct nas_task *sender = nas_current_task;
    nas_arch_save_fpu(sender);
    nas_current_task = target;
    nas_arch_restore_cpu(target);
    nas_sched_child_exit(0, NAS_SIGKILL);
    nas_current_task = sender;
    nas_arch_restore_cpu(sender);
}
uint64_t nas_signal_kill(uint64_t raw_pid, uint64_t signal, uint64_t *frame) {
    int64_t pid = (int64_t)(int32_t)(uint32_t)raw_pid;
    if (signal > 64) return (uint64_t)-EINVAL;
    if (signal && signal != NAS_SIGKILL)
        return (uint64_t)-EOPNOTSUPP; /* no fake signal delivery */
    struct nas_task *sender = nas_current_task;
    struct nas_task *matches[NAS_TASK_LIMIT];
    unsigned count = 0;
    bool protected_init = false, self = false;
    for (unsigned i=0;i<NAS_TASK_LIMIT;i++) {
        struct nas_task *target = nas_task_at(i);
        if (!signal_target_matches(target,pid,sender)) continue;
        if (signal && target->pid==1) {
            protected_init=true; /* never terminate the OS supervisor */
            continue;
        }
        matches[count++]=target;
    }
    if (!count) return (uint64_t)(protected_init ? -EPERM : -ESRCH);
    if (!signal) return 0;
    /* Snapshot group before termination and do not skip zombie identity.
     * Kill the caller LAST: it must never return to its former user RIP. */
    for (unsigned i=0;i<count;i++) {
        struct nas_task *target=matches[i];
        if (target==sender) { self=true; continue; }
        if (target->state==NAS_TASK_ZOMBIE) continue;
        signal_kill_other(target);
    }
    if (self) {
        nas_sched_child_exit(0,NAS_SIGKILL);
        return nas_task_schedule(frame);
    }
    return 0;
}
