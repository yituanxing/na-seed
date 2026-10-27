#ifndef NASEED_SCHEDULER_SERVICE_H
#define NASEED_SCHEDULER_SERVICE_H
/* Called from the single native Task dispatcher: tick kernel-owned
 * deadlines and run a bounded kernel-worker slice. Producers/IRQs only
 * mark readiness and never resume C continuations directly. */
void nas_sched_service_kernel(void);
#endif
