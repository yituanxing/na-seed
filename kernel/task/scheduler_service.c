#include <stdint.h>
#include "scheduler_service.h"
#include "kthread.h"
#include "sched_core.h"
#include "../drivers/core/bus.h"
#include "../errno.h"
#include "../arch/arch.h"

/* The composition root installs an ordered, fixed-capacity set of native
 * scheduler callbacks once at boot. This is NOT a second task scheduler:
 * no dynamic allocation, blocking, worker creation or driver dispatch here.
 * Registration never occurs in the IRQ path or while callbacks execute. */
#define NAS_SCHED_DEFERRED_LIMIT 12U
struct deferred_service {
    nas_sched_deferred_fn run;
    void *context;
};
static struct deferred_service services[2][NAS_SCHED_DEFERRED_LIMIT];
static unsigned service_count[2];
static bool in_services;

int nas_sched_register_deferred(enum nas_sched_service_phase phase,
                                nas_sched_deferred_fn service,void *context) {
    if((unsigned)phase>NAS_SCHED_AFTER_WORKERS || !service)return -EINVAL;
    if(in_services)return -EBUSY;
    const unsigned index=(unsigned)phase;
    for(unsigned i=0;i<service_count[index];i++)
        if(services[index][i].run==service &&
           services[index][i].context==context)return -EEXIST;
    if(service_count[index]>=NAS_SCHED_DEFERRED_LIMIT)return -ENOSPC;
    services[index][service_count[index]++]=
        (struct deferred_service){.run=service,.context=context};
    return 0;
}

static void run_services(enum nas_sched_service_phase phase) {
    in_services=true;
    const unsigned count=service_count[(unsigned)phase];
    for(unsigned i=0;i<count;i++) {
        const struct deferred_service *owner=&services[(unsigned)phase][i];
        owner->run(owner->context);
    }
    in_services=false;
}

/* Exactly one native kernel C-frame slice per user scheduling boundary.
 * Every additional user Task remains eligible on the same boundary.
 * A yielding, always-ready worker cannot monopolize a user process. */
void nas_sched_service_kernel(void) {
    run_services(NAS_SCHED_BEFORE_WORKERS);
    (void)nas_kthread_timers_tick(nas_arch_monotonic_ns());
    if(nas_sched_allow_kernel_slice(nas_sched_has_ready_users(),
                                    nas_kthread_runnable()))
        (void)nas_kthread_run(1);
    run_services(NAS_SCHED_AFTER_WORKERS);
    /* Driver reclamation must never run inside an IRQ or a lwIP callback.
     * One bounded retry per ordinary scheduling boundary is enough to
     * complete a queued hot-unbind after its final consumer drops a pin. */
    (void)nas_bus_service_removals(1);
}
