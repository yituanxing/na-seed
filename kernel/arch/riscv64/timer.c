#include <stdint.h>
#include <stdbool.h>
#include "../../panic.h"
#include "../../task.h"
#include "../../task/sched.h"
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
void nas_rv_timer_handle(struct nas_rv_trap_frame *frame) {
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
