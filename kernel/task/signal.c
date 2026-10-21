#include <stdint.h>
#include "../errno.h"
#include "../task.h"
#include "../mm/usercopy.h"
#include "signal.h"
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
