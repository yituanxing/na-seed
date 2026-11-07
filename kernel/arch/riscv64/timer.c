#include <stdint.h>
#include <stdbool.h>
#include "../../panic.h"
#include "../../task.h"
#include "../../task/sched.h"
#include "../../errno.h"
#include "../../drivers/core/irq.h"
#include "../arch.h"
#include "context.h"

/* QEMU virt/OpenSBI timebase is 10 MHz. The privileged time CSR alone
 * does not encode its frequency; this first hardware provider is
 * deliberately QEMU-virt-specific, not an invented portable Hz.
 * Configure the actual SBI v0.2 TIME extension (0x54494d45).
 */
#define QEMU_VIRT_TIMEBASE_HZ UINT64_C(10000000)
#define RV_TIMER_HZ 100ULL
#define SBI_TIME UINT64_C(0x54494d45)
#define SBI_BASE 0x10ULL
#define RV_SCAUSE_INTERRUPT (UINT64_C(1) << 63)
#define RV_SCAUSE_SUPERVISOR_TIMER (RV_SCAUSE_INTERRUPT | 5U)
#define RV_SCAUSE_SUPERVISOR_EXTERNAL (RV_SCAUSE_INTERRUPT | 9U)
#define RV_SIE_EXTERNAL (UINT64_C(1) << 9)
#define RV_PLIC_FIRST_VIRTIO_SOURCE 1U
#define RV_PLIC_LAST_VIRTIO_SOURCE 8U
/* QEMU virt PLIC: all three views are independently mapped supervisor IO
 * pages.  The low-level page mapper must not be asked to map the intervening
 * 2 MiB sparse gap (PLIC is not kernel RAM/HHDM).  This one-hart target uses
 * hart 0's S-mode context (enable +0x2080, claim/complete +0x201004). */
static volatile uint32_t *plic_priority;
static volatile uint32_t *plic_enable;
static volatile uint32_t *plic_context;
void nas_rv_plic_attach(volatile uint32_t *priority,
                        volatile uint32_t *enable,
                        volatile uint32_t *context) {
    if (!priority || !enable || !context || plic_context)
        nas_fail("RV64 PLIC double/invalid mapping");
    plic_priority=priority;
    plic_enable=enable;
    plic_context=context;
    /* Threshold zero admits only explicitly priority-enabled sources.
     * Preserve STIE while enabling SEIE; no global SIE in kernel C. */
    plic_enable[0]=0;
    plic_context[0]=0;
    __asm__ volatile("fence iorw,iorw" : : : "memory");
    __asm__ volatile("csrs sie,%0" : : "r"(RV_SIE_EXTERNAL) : "memory");
}
int nas_arch_irq_route_enable(unsigned hwirq) {
    if (hwirq < RV_PLIC_FIRST_VIRTIO_SOURCE ||
        hwirq > RV_PLIC_LAST_VIRTIO_SOURCE) return -EINVAL;
    if (!plic_context) return -ENODEV;
    plic_priority[hwirq]=1U;
    __asm__ volatile("fence iorw,iorw" : : : "memory");
    plic_enable[0] |= 1U << hwirq;
    __asm__ volatile("fence iorw,iorw" : : : "memory");
    return 0;
}
void nas_arch_irq_route_disable(unsigned hwirq) {
    if (!plic_context || hwirq < RV_PLIC_FIRST_VIRTIO_SOURCE ||
        hwirq > RV_PLIC_LAST_VIRTIO_SOURCE) return;
    plic_enable[0] &= ~(1U << hwirq);
    __asm__ volatile("fence iorw,iorw" : : : "memory");
    plic_priority[hwirq]=0U;
    __asm__ volatile("fence iorw,iorw" : : : "memory");
}
void nas_rv_plic_external_irq(void) {
    if (!plic_context) nas_fail("RV64 external IRQ before PLIC mapping");
    /* A claim of zero means the IRQ has been withdrawn; never complete 0.
     * Class-side VirtIO acknowledgement occurs inside the shared dispatch,
     * BEFORE PLIC completion, or a level interrupt will retrigger forever. */
    for (unsigned budget=0;budget<32U;++budget) {
        uint32_t source=plic_context[1];
        if (!source) return;
        __asm__ volatile("fence iorw,iorw" : : : "memory");
        if (source < RV_PLIC_FIRST_VIRTIO_SOURCE ||
            source > RV_PLIC_LAST_VIRTIO_SOURCE)
            nas_fail("RV64 unexpected PLIC external source");
        (void)nas_irq_dispatch(source);
        __asm__ volatile("fence iorw,iorw" : : : "memory");
        plic_context[1]=source;
        __asm__ volatile("fence iorw,iorw" : : : "memory");
    }
    /* A hard finite bound prevents a stuck level source from monopolizing
     * interrupt context; a later IRQ can claim the remaining work. */
}
struct sbi_result { int64_t error, value; };
static struct sbi_result sbi_call(uint64_t eid, uint64_t fid, uint64_t arg0) {
    register uint64_t a0 __asm__("a0")=arg0;
    register uint64_t a1 __asm__("a1")=0;
    register uint64_t a6 __asm__("a6")=fid;
    register uint64_t a7 __asm__("a7")=eid;
    __asm__ volatile("ecall" : "+r"(a0), "+r"(a1) :
                     "r"(a6), "r"(a7) : "memory");
    return (struct sbi_result){(int64_t)a0,(int64_t)a1};
}
/* QEMU virt hardware time CSR remains readable with supervisor IRQs masked.
 * The frequency is QEMU-virt-specific until FDT timebase-frequency exists. */
uint64_t nas_arch_monotonic_ns(void) {
    uint64_t ticks;
    __asm__ volatile("rdtime %0" : "=r"(ticks));
    return (ticks / QEMU_VIRT_TIMEBASE_HZ) * 1000000000ULL +
           (ticks % QEMU_VIRT_TIMEBASE_HZ) * 1000000000ULL / QEMU_VIRT_TIMEBASE_HZ;
}
uint64_t nas_arch_clock_resolution_ns(void) {
    return 1000000000ULL / QEMU_VIRT_TIMEBASE_HZ;
}
static void arm_next_tick(void) {
    uint64_t now;
    __asm__ volatile("rdtime %0" : "=r"(now));
    if(sbi_call(SBI_TIME,0,now+QEMU_VIRT_TIMEBASE_HZ/RV_TIMER_HZ).error)
        nas_fail("RV64 SBI TIME set_timer rejected");
}
void nas_rv_timer_start(void) {
    struct sbi_result ext=sbi_call(SBI_BASE,3,SBI_TIME);
    if(ext.error || !ext.value) nas_fail("RV64 SBI TIME unavailable");
    /* Preserve the original S-mode interrupt masking contract.
     * SIE is clear while kernel C executes; SPIE is restored on sret.
     */
    uint64_t stie=1ULL<<5;
    __asm__ volatile("csrs sie,%0" : : "r"(stie) : "memory");
    arm_next_tick();
}
/* For the IRQ-masked native scheduler only. This temporary S-mode stvec
 * never swaps sscratch/sp or captures a user Task frame. The SBI timer
 * continuously rearms, so each wake forces a fresh Task readiness scan. */
extern void nas_rv_idle_trap(void);
extern void nas_rv_full_trap(void);
void nas_arch_idle_wait(void) {
    __asm__ volatile(
        "la t0,nas_rv_idle_trap\n"
        "csrw stvec,t0\n"
        "csrsi sstatus,2\n"  /* SIE */
        "wfi\n"
        "csrci sstatus,2\n"
        "la t0,nas_rv_full_trap\n"
        "csrw stvec,t0\n"
        : : : "t0","memory");
}
void nas_rv_idle_irq_c(void) {
    uint64_t cause, status;
    __asm__ volatile("csrr %0,scause" : "=r"(cause));
    __asm__ volatile("csrr %0,sstatus" : "=r"(status));
    if (!(status & (1ULL<<8)))
        nas_fail("RV64 idle interrupt lost supervisor SPP");
    if (cause==RV_SCAUSE_SUPERVISOR_TIMER) {
        nas_irq_note_hardware();
        arm_next_tick();
    }
    else if (cause==RV_SCAUSE_SUPERVISOR_EXTERNAL)
        nas_rv_plic_external_irq();
    else nas_fail("RV64 idle unexpected supervisor interrupt");
}
void nas_rv_timer_handle(struct nas_rv_trap_frame *frame) {
    nas_irq_note_hardware();
    arm_next_tick(); /* OpenSBI clears the pending STIP when rearmed. */
    struct nas_task *from=nas_current_task;
    if(!from || !nas_task_kernel_stack_ok(from)) nas_fail("RV64 timer invalid task");
    nas_arch_save_user_frame(from,0);
    nas_arch_save_fpu(from);
    /* The native scheduler parks this IRQ C stack and restores precisely
     * this Task's original SRET frame, never a foreign user-frame copy. */
    nas_sched_preempt_select();
    (void)frame;
}
