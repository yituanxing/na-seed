#ifndef NASEED_SCHEDULER_SERVICE_H
#define NASEED_SCHEDULER_SERVICE_H
/* Shared between syscall-boundary and native user timer IRQ scheduler.
 * Tick kernel-owned deadlines and run a bounded slice of ready kernel
 * continuations, never scheduling directly in a producer/interrupt callback.
 * This does not replace the Linux-user saved-frame dispatcher yet. */
void nas_sched_service_kernel(void);
#endif
