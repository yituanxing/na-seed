#include <stdint.h>
#include "../../task.h"
#include "../../task/sched.h"
#include "../arch.h"
#include "../../panic.h"
#include "irq.h"

/* Single CPU: the IDT interrupt gate masks IF and syscall FMASK masks IF.
 * The trap frame is live on the interrupted task's kernel entry stack.
 * Rescheduling occurs only after C call frames unwind to the IRET boundary.
 * No interrupt in kernel mode is treated as a runnable user snapshot. */
static uint64_t irq_ticks;
static uint64_t tsc_epoch, tsc_hz;
static uint8_t pit_inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1,%0" : "=a"(val) : "Nd"(port));
    return val;
}
static uint16_t pit_count(void) {
    /* Latch PIT channel 0 while the BSP has interrupts disabled. */
    nas_arch_outb(0x43,0x00);
    uint16_t lo=pit_inb(0x40), hi=pit_inb(0x40);
    return (uint16_t)(lo | (hi << 8));
}
static uint64_t read_tsc(void) {
    uint32_t lo,hi;
    __asm__ volatile("rdtsc" : "=a"(lo),"=d"(hi));
    return ((uint64_t)hi<<32) | lo;
}
/* Calibrate against the ACTUAL PIT hardware period, not a guessed TSC
 * frequency. IRQ0 remains masked until the user IRET. This calibration
 * runs only once at BSP initialization, before any scheduler/Task waits. */
void nas_x86_clock_init(void) {
    uint16_t last=pit_count();
    unsigned wraps=0;
    uint64_t first=0, last_tsc=0;
    /* Mode 3 counter counts down and reloads (a rising sample is a wrap).
     * Bound the probe so unsupported virtual PITs fail rather than hang. */
    uint64_t watchdog=read_tsc();
    for (;;) {
        uint16_t current=pit_count();
        if(current>last) {
            wraps++;
            if(wraps==1) first=read_tsc();
            if(wraps==5) { last_tsc=read_tsc(); break; }
        }
        last=current;
        if(read_tsc()-watchdog>5000000000ULL)
            nas_fail("x86 PIT/TSC calibration timed out");
    }
    uint64_t cycles=last_tsc-first;
    tsc_hz=(cycles/4)*100ULL; /* 4 PIT periods at approximately 100Hz. */
    if(tsc_hz<1000000ULL) nas_fail("x86 TSC clock calibration invalid");
    tsc_epoch=last_tsc;
}
uint64_t nas_arch_monotonic_ns(void) {
    if(!tsc_hz) nas_fail("x86 monotonic clock before PIT calibration");
    uint64_t elapsed=read_tsc()-tsc_epoch;
    return (elapsed/tsc_hz)*1000000000ULL +
           (elapsed%tsc_hz)*1000000000ULL/tsc_hz;
}
uint64_t nas_arch_clock_resolution_ns(void) {
    return 1000000ULL; /* conservative millisecond accuracy after PIT sync */
}
void nas_x86_timer_irq(struct nas_x86_user_irq_frame *frame) {
    if ((frame->cs & 3U) != 3U)
        nas_fail("x86 timer interrupted kernel C code before IRQ-safe scheduling");
    struct nas_task *from=nas_current_task;
    if (!from || !nas_task_kernel_stack_ok(from))
        nas_fail("x86 timer without a valid running task");
    irq_ticks++;
    /* A native switch can suspend this entire IRQ C stack, so EOI
     * must be sent before we switch or subsequent ticks are blocked. */
    nas_arch_outb(0x20,0x20);
    if (from->state == NAS_TASK_RUNNING) {
        for (unsigned i=0;i<15;i++)
            from->arch.resume_regs[i]=frame->regs[i];
        from->arch.resume_user_rip=frame->rip;
        from->arch.resume_user_rflags=frame->rflags;
        from->arch.resume_user_rsp=frame->rsp;
        nas_arch_save_fpu(from);
        from->native_trap_frame=(uint64_t)(uintptr_t)frame;
        (void)nas_sched_preempt_select();
    }
    /* PIC acknowledged before the possible native stack switch. */
}
