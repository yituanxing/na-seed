#include <stdint.h>
#include "scheduler_service.h"
#include "kthread.h"
#include "sched_core.h"
#include "../drivers/core/bus.h"
#ifdef NAS_ENABLE_VIRTIO_INPUT
#include "../drivers/input/virtio_input.h"
#endif
#ifdef NAS_ENABLE_VIRTIO_GPU
#include "../drivers/display/display.h"
#include "../drivers/display/virtio_gpu.h"
#include "../panic.h"
#endif
#ifdef NAS_ENABLE_VIRTIO_BLK_ASYNC
#include "../../ports/uboot/async_blk.h"
#endif
#ifdef NAS_ENABLE_M179_FS_IRQ
/* The real lwext4 worker is activated only after M178's hardware oracle
 * finished. This scheduler-safe step schedules it; it does not execute
 * upstream ext4 or suspend a C frame here. */
extern void nas_lwext4_async_runtime_step(void);
extern void nas_init_process_deferred_exit_service(void);
#endif
#include "../arch/arch.h"
/* Exactly one native kernel C-frame slice per user scheduling boundary.
 * Every additional user Task remains eligible on the same boundary.
 * A yielding, always-ready worker cannot monopolize a user process. */
void nas_sched_service_kernel(void) {
#ifdef NAS_ENABLE_VIRTIO_BLK_ASYNC
    /* IRQ acknowledges only this hardware source. A bounded scheduler-side
     * drain owns buffer sync, completion callbacks and next test actions. */
    nas_async_blk_service();
    nas_async_blk_native_test_step();
#endif
#ifdef NAS_ENABLE_M179_FS_IRQ
    nas_lwext4_async_runtime_step();
#endif
#ifdef NAS_ENABLE_VIRTIO_GPU
    /* fbdev pwrite is internally copied in 1 KiB chunks. Only at this
     * scheduler-safe boundary may a GPU submit its coalesced dirty scanout;
     * never issue one VirtIO command for every kernel usercopy chunk. */
    if(nas_display_primary() && nas_display_present()<0)
        nas_fail("GPU scanout update failed");
    nas_virtio_gpu_report();
#endif
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
#ifdef NAS_ENABLE_M179_FS_IRQ
    /* PID1 completion is a scheduler-owned event, not proof that the IRQ
     * filesystem worker finished. Commit test success only after its real
     * ext4 unmount/flush; the normal idle path waits for further IRQs. */
    nas_init_process_deferred_exit_service();
#endif
    /* Driver reclamation must never run inside an IRQ or a lwIP callback.
     * One bounded retry per ordinary scheduling boundary is enough to
     * complete a queued hot-unbind after its final consumer drops a pin. */
    (void)nas_bus_service_removals(1);
}
