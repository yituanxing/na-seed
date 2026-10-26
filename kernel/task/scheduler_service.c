#include <stdint.h>
#include "scheduler_service.h"
#include "kthread.h"
#include "sched_core.h"
#include "../arch/arch.h"
/* Exactly one native kernel C-frame slice per user scheduling boundary.
 * Every additional user Task remains eligible on the same boundary.
 * A yielding, always-ready worker cannot monopolize a user process. */
void nas_sched_service_kernel(void) {
    (void)nas_kthread_timers_tick(nas_arch_monotonic_ns());
    if(nas_sched_allow_kernel_slice(nas_sched_has_ready_users(),
                                    nas_kthread_runnable()))
        (void)nas_kthread_run(1);
}
