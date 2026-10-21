#include <stdint.h>
#include <stdbool.h>
#include "../errno.h"
#include "../task.h"
#include "../arch/arch.h"
#include "../arch/mmu.h"
#include "../fd.h"
#include "../mm/usercopy.h"
#include "process.h"
extern void nas_print(const char *);
extern void nas_dec(uint64_t);
static void proc_fail(const char *s) __attribute__((noreturn));
static void proc_fail(const char *s) { nas_print("[FAIL] M1 "); nas_print(s); nas_print("\n"); nas_arch_halt(); }
/* A cooperative child-first fork; parent remains READY while child runs.
 * Once a child blocks on a pipe, another READY task runs and the blocked
 * syscall is completed later in its owner's address space. No timer
 * preemption, SMP, or independent kernel stack is claimed.
 */
uint64_t nas_process_fork(uint64_t *frame) {
    struct nas_task *parent = nas_current_task;
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
    nas_arch_save_user_frame(parent, frame);
    nas_arch_set_saved_return(parent, child->pid);
    parent->resume_valid = true;
    parent->blocked_nr = 0;
    parent->pending_valid = false;
    parent->state = NAS_TASK_READY;
    child->state = NAS_TASK_RUNNING;
    nas_current_task = child;
    nas_arch_restore_cpu(child);
    nas_print("[PROC] fork parent="); nas_dec(parent->pid);
    nas_print(" child="); nas_dec(child->pid); nas_print("\n");
    return 0; /* fork returns zero in the child */
}
uint64_t nas_process_wait4(uint64_t desired, uint64_t status_addr,
                           uint64_t options, uint64_t rusage) {
    if (options & ~1ULL) return (uint64_t)-EINVAL;
    if (rusage) return (uint64_t)-EOPNOTSUPP;
    bool have_child = false;
    /* POSIX -1 waits for any child; direct positive PID is also valid.
     * This first model only supports a single process group.
     */
    if ((int64_t)desired == 0 || (int64_t)desired < -1)
        return (uint64_t)-EOPNOTSUPP;
    for (unsigned slot = 0; slot < NAS_TASK_LIMIT; ++slot) {
        struct nas_task *child = nas_task_child_at(nas_current_task->pid, slot);
        if (!child) continue;
        uint64_t pid = child->pid;
        if (desired != UINT64_MAX && desired != pid) continue;
        have_child = true;
        if (child->state != NAS_TASK_ZOMBIE) continue;
        uint64_t code = child->termination_signal
            ? child->termination_signal : (child->exit_code & 255U) << 8;
        if (status_addr) {
            uint8_t status_bytes[4] = {
                (uint8_t)code, (uint8_t)(code >> 8), 0, 0
            };
            if (!nas_user_write_bytes(status_addr, status_bytes, 4))
                return (uint64_t)-EFAULT;
        }
        if (!nas_mmu_reclaim_root(nas_task_page_root(child)))
            proc_fail("wait4 failed to reclaim child address space");
        nas_print("[PROC] wait4 reaped pid="); nas_dec(pid);
        nas_print(" status="); nas_dec(code); nas_print("\n");
        nas_task_release(child);
        return pid;
    }
    return have_child ? ((options & 1) ? 0 : (uint64_t)-EOPNOTSUPP)
                      : (uint64_t)-ECHILD;
}
