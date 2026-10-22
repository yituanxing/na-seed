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
    if(nas_sched_preempt_select()) {
        nas_arch_load_user_frame(nas_current_task,0);
        static unsigned observed;
        if(observed++<8) {
            extern void nas_print(const char*);
            extern void nas_dec(uint64_t);
            nas_print("[M28] timer-preempt from=");
            nas_dec(from->pid);
            nas_print(" to=");nas_dec(nas_current_task->pid);
            nas_print("\n");
        }
    }
    (void)frame;
}
