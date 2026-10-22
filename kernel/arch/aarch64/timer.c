#include <stdint.h>
#include "../../task.h"
#include "../../task/sched.h"
#include "../../panic.h"
#include "../arch.h"
#include "context.h"

/* QEMU 'virt' GICv2: distributor 0x08000000, CPU interface 0x08010000.
 * ARM Generic Virtual Timer is EL1-visible CNTV_*, signaled on PPI 27.
 * Register offsets are the GICv2 architectural MMIO interface, not an
 * invented NaSeed-specific interrupt protocol.
 */
#define GICD_PHYS UINT64_C(0x08000000)
#define GICC_PHYS UINT64_C(0x08010000)
#define GICD_VIRT UINT64_C(0xffffffffe0001000)
#define GICC_VIRT UINT64_C(0xffffffffe0002000)
#define ARM_VTIMER_PPI 27U
extern bool nas_arm_map_io_page(uint64_t phys, uint64_t virt);
static volatile uint32_t *dist;
static volatile uint32_t *iface;
static uint64_t period;
/* CNTVCT keeps increasing while IRQ delivery is masked, including while
 * the last process is asleep in the syscall-boundary idle dispatcher. */
uint64_t nas_arch_monotonic_ns(void) {
    uint64_t ticks, hz;
    __asm__ volatile("mrs %0,cntvct_el0" : "=r"(ticks));
    __asm__ volatile("mrs %0,cntfrq_el0" : "=r"(hz));
    if(!hz) nas_fail("ARM64 monotonic counter has no frequency");
    return (ticks / hz) * 1000000000ULL +
           (ticks % hz) * 1000000000ULL / hz;
}
uint64_t nas_arch_clock_resolution_ns(void) {
    uint64_t hz;
    __asm__ volatile("mrs %0,cntfrq_el0" : "=r"(hz));
    return hz ? (1000000000ULL + hz - 1) / hz : 0;
}
static void rearm_timer(void) {
    uint64_t now;
    __asm__ volatile("mrs %0,cntvct_el0" : "=r"(now));
    __asm__ volatile("msr cntv_cval_el0,%0\n isb"
                     : : "r"(now+period) : "memory");
}
void nas_arm_timer_start(void) {
    if (!nas_arm_map_io_page(GICD_PHYS,GICD_VIRT) ||
        !nas_arm_map_io_page(GICC_PHYS,GICC_VIRT))
        nas_fail("ARM64 GICv2 device mapping failed");
    dist=(volatile uint32_t *)(uintptr_t)GICD_VIRT;
    iface=(volatile uint32_t *)(uintptr_t)GICC_VIRT;
    uint64_t frequency;
    __asm__ volatile("mrs %0,cntfrq_el0" : "=r"(frequency));
    if(!frequency || frequency<100) nas_fail("ARM64 CNTFRQ invalid");
    period=frequency/100; /* exact hardware-counter rate, no guesswork */
    /* IRQ27: PPI, banked ENABLE[0] for the one QEMU vCPU. No SGIs or
     * arbitrary device IRQs are accidentally enabled by this driver. */
    volatile uint8_t *priority=(volatile uint8_t *)(void *)dist;
    priority[0x400+ARM_VTIMER_PPI]=0x80;
    dist[0x100/4] = 1U<<ARM_VTIMER_PPI;
    dist[0x0/4] |= 1; /* group 0 distributor enable */
    iface[0x04/4]=0xff; /* priority mask */
    iface[0x00/4]=1;   /* group 0 CPU interface */
    rearm_timer();
    __asm__ volatile("msr cntv_ctl_el0,%0\n isb"
                     : : "r"(1ULL) : "memory");
    extern void nas_print(const char *);
    nas_print("[M28 ARM64] Generic Virtual Timer/GIC PPI27 active\n");
}
/* Handler runs with PSTATE.I=1; user context fully saved in trap.S.
 * IAR/EOIR must be paired even when the scheduler chooses another Task.
 */
void nas_arm_timer_irq_c(struct nas_arm_trap_frame *frame) {
    uint32_t id=iface[0x0c/4];
    if((id & 0x3ffU) != ARM_VTIMER_PPI)
        nas_fail("ARM64 unexpected GIC interrupt ID");
    rearm_timer();
    struct nas_task *from=nas_current_task;
    if(!from || !nas_task_kernel_stack_ok(from))
        nas_fail("ARM64 timer invalid Task");
    nas_arch_save_user_frame(from,0);
    nas_arch_save_fpu(from);
    if(nas_sched_preempt_select()) {
        nas_arch_load_user_frame(nas_current_task,0);
        static unsigned observed;
        if(observed++<8) {
            extern void nas_print(const char *);
            extern void nas_dec(uint64_t);
            nas_print("[M28] timer-preempt from=");
            nas_dec(from->pid);
            nas_print(" to=");nas_dec(nas_current_task->pid);
            nas_print("\n");
        }
    }
    iface[0x10/4]=id;
    __asm__ volatile("dsb sy" : : : "memory");
    (void)frame;
}
