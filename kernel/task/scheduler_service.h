#ifndef NASEED_SCHEDULER_SERVICE_H
#define NASEED_SCHEDULER_SERVICE_H
/* Boot-composed, statically owned deferred services. Hardware IRQs only
 * publish readiness; callbacks execute on the independent Task scheduler
 * stack, never inside an IRQ, an arbitrary syscall frame, or a worker.
 * Registration is boot-only; the registrations outlive hot-unbound devices.
 * Drivers must check whether their own device instance is still present. */
enum nas_sched_service_phase {
    NAS_SCHED_BEFORE_WORKERS = 0,
    NAS_SCHED_AFTER_WORKERS = 1
};
typedef void (*nas_sched_deferred_fn)(void *context);
int nas_sched_register_deferred(enum nas_sched_service_phase phase,
                                nas_sched_deferred_fn service,void *context);
/* Called from the single native Task dispatcher: tick kernel-owned
 * deadlines and run a bounded kernel-worker slice. Producers/IRQs only
 * mark readiness and never resume C continuations directly. */
void nas_sched_service_kernel(void);
#endif
