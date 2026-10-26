#ifndef NASEED_SCHED_CORE_H
#define NASEED_SCHED_CORE_H
#include <stdbool.h>
struct nas_task;
/* One user Task readiness rule across syscall and timer IRQ scheduling.
 * Callers still own architecture trap-frame and MM/TLS/FPU transitions;
 * this deliberately does not synthesize kernel continuations for users. */
bool nas_sched_user_eligible(const struct nas_task *candidate,
                             const struct nas_task *running,unsigned pass);
/* Domain fairness: bound native kernel work while user Tasks are runnable.
 * The actual C-stack swap remains in the kernel worker provider. */
bool nas_sched_allow_kernel_slice(bool user_ready,bool kernel_ready);
bool nas_sched_has_ready_users(void);
#endif
