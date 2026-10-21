#include <stdint.h>
#include <stdbool.h>
#include "context.h"
#include "../arch.h"
#include "../mmu.h"
#include "../../task.h"
#include "../../syscall/dispatch.h"
#include "../../panic.h"
#include "../../drivers/serial.h"
static struct nas_arm_trap_frame *live;
extern void nas_arm_save_simd(void *);
extern void nas_arm_load_simd(const void *);
void nas_arm_full_trap_c(struct nas_arm_trap_frame *frame) {
    uint64_t esr, far;
    __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
    __asm__ volatile("mrs %0, far_el1" : "=r"(far));
    /* EC=0x15 is the AArch64 EL0 SVC instruction; other exceptions
     * are real hardware failures, not a pretend syscall success. */
    if ((esr>>26) != 0x15ULL || (frame->spsr_el1 & 15U)) {
        nas_print("[ARM64 FAULT] esr="); nas_hex(esr);
        nas_print(" elr="); nas_hex(frame->elr_el1);
        nas_print(" far="); nas_hex(far);
        nas_print(" sp="); nas_hex(frame->sp_el0);
        nas_print("\n");
        nas_fail("unexpected AArch64 EL0 synchronous exception");
    }
    frame->elr_el1+=4; /* SVC #0 instruction is 4 bytes. */
    uint64_t call[15] = {frame->x[8],frame->x[0],frame->x[1],
        frame->x[2],frame->x[3],frame->x[4],frame->x[5],frame->elr_el1};
    live=frame;
    uint64_t result=syscall_dispatch(call);
    frame->x[0]=result;
    live=0;
}
void nas_arch_restore_cpu(const struct nas_task *task) {
    nas_mmu_switch_root(task->root_phys);
    uint64_t tls=nas_task_tls_base(task);
    __asm__ volatile("msr tpidr_el0,%0" : : "r"(tls) : "memory");
    nas_arm_load_simd(task->arch.simd_regs);
}
void nas_arch_save_fpu(struct nas_task *task) {
    nas_arm_save_simd(task->arch.simd_regs);
}
void nas_arch_save_user_frame(struct nas_task *task,const uint64_t *args) {
    (void)args;
    if(!live) nas_fail("ARM context save outside EL0 trap");
    for(unsigned i=0;i<31;i++) task->arch.user_regs[i]=live->x[i];
    task->arch.user_sp=live->sp_el0;
    task->arch.user_elr=live->elr_el1;
    task->arch.user_spsr=live->spsr_el1;
    task->arch.user_tls_base=live->tpidr_el0;
}
void nas_arch_set_saved_return(struct nas_task *task,uint64_t value) {
    task->arch.user_regs[0]=value;
}
uint64_t nas_arch_saved_return(const struct nas_task *task) {
    return task->arch.user_regs[0];
}
void nas_arch_load_user_frame(const struct nas_task *task,uint64_t *args) {
    (void)args;
    if(!live) nas_fail("ARM context load outside EL0 trap");
    for(unsigned i=0;i<31;i++) live->x[i]=task->arch.user_regs[i];
    live->sp_el0=task->arch.user_sp;
    live->elr_el1=task->arch.user_elr;
    live->spsr_el1=task->arch.user_spsr;
    live->tpidr_el0=nas_task_tls_base(task);
}
