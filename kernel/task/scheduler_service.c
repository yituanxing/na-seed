#include <stdint.h>
#include "scheduler_service.h"
#include "kthread.h"
#include "sched_core.h"
#include "../drivers/core/bus.h"
#ifdef NAS_ENABLE_VIRTIO_INPUT
#include "../drivers/input/virtio_input.h"
#endif
#include "../arch/arch.h"
/* Exactly one native kernel C-frame slice per user scheduling boundary.
 * Every additional user Task remains eligible on the same boundary.
 * A yielding, always-ready worker cannot monopolize a user process. */
void nas_sched_service_kernel(void) {
#ifdef NAS_ENABLE_VIRTIO_INPUT
    /* Hardware IRQ handlers ACK only their VirtIO source and atomically
     * mark pending work; real queue/input/evdev work runs here. Routed
     * devices are never polled without a pending interrupt. Historical
     * fake buses lacking IRQ routes retain their poll-based host contract. */
    (void)nas_virtio_input_poll(64U);
    nas_virtio_input_irq_report();
#endif
    (void)nas_kthread_timers_tick(nas_arch_monotonic_ns());
    if(nas_sched_allow_kernel_slice(nas_sched_has_ready_users(),
                                    nas_kthread_runnable()))
        (void)nas_kthread_run(1);
    /* Driver reclamation must never run inside an IRQ or a lwIP callback.
     * One bounded retry per ordinary scheduling boundary is enough to
     * complete a queued hot-unbind after its final consumer drops a pin. */
    (void)nas_bus_service_removals(1);
}
