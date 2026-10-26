#include <stdint.h>
#include <stdbool.h>
#include "context.h"
extern void nas_arm_timer_irq_c(struct nas_arm_trap_frame *frame);
#include "../arch.h"
#include "../mmu.h"
#include "../../task.h"
#include "../../syscall/dispatch.h"
#include "../../task/signal.h"
#include "../../task/ptrace.h"
#include "../../task/sched.h"
#include "../../panic.h"
#include "../../drivers/serial.h"
#include "../../lib/string.h"
static struct nas_arm_trap_frame *live;
uint64_t nas_arm_entry_stack_top;
/* ARM trap.S reserves 288 bytes: 280-byte register image + 8 padding,
 * keeping the active EL1 stack 16-byte aligned through all C calls. */
uint64_t nas_arm_return_stack(const struct nas_arm_trap_frame *source) {
    const struct nas_task *task=nas_current_task;
    if (!nas_task_kernel_stack_ok(task))
        nas_fail("ARM64 invalid task kernel stack at eret");
    uint64_t dest=task->kernel_stack_top-288U;
    if (dest != (uint64_t)(uintptr_t)source)
        memcpy((void *)(uintptr_t)dest, source, sizeof(*source));
    return dest;
}

extern void nas_arm_save_simd(void *);
extern void nas_arm_load_simd(const void *);
/* Unlike SVC, IRQ does not modify ELR and must not fabricate x0. */
void nas_arm_lower_irq_c(struct nas_arm_trap_frame *frame) {
    if(frame->spsr_el1 & 15U)
        nas_fail("ARM64 timer interrupted non-EL0 execution");
    live=frame;
    nas_arm_timer_irq_c(frame);
    nas_signal_user_return((uint64_t *)(void *)frame);
    live=0;
}
void nas_arm_full_trap_c(struct nas_arm_trap_frame *frame) {
    uint64_t esr, far;
    __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
    __asm__ volatile("mrs %0, far_el1" : "=r"(far));
    /* EC 0x3c is a real AArch64 user BRK instruction (GDB software
     * breakpoint), not an unexpected kernel fault. The ELR points to
     * the BRK instruction itself and MUST NOT be advanced. */
    if ((esr>>26)==0x3cULL && !(frame->spsr_el1&15U)) {
        struct nas_task *tracee=nas_current_task;
        live=frame;
        nas_arch_save_user_frame(tracee,0);
        nas_arch_save_fpu(tracee);
        if(nas_ptrace_user_exception(tracee,NAS_SIGTRAP,
                                      frame->elr_el1,frame->sp_el0)) {
            static unsigned brk_reports;
            if(brk_reports++<15) {
                nas_print("[M75 ARM64 BRK] pid=");nas_dec(tracee->pid);
                nas_print(" pc=");nas_hex(frame->elr_el1);
                nas_print(" tracer=");nas_dec(tracee->ptrace_tracer_pid);
                nas_print("\n");
            }
            (void)nas_task_schedule((uint64_t *)(void *)frame);
            nas_signal_user_return((uint64_t *)(void *)frame);
            live=0;
            return;
        }
        live=0;
    }
    /* EC=0x15 is AArch64 EL0 SVC, otherwise use the genuine fault
     * report rather than manufacturing a successful syscall. */
    if ((esr>>26) != 0x15ULL || (frame->spsr_el1 & 15U)) {
        nas_print("[ARM64 FAULT] esr="); nas_hex(esr);
        nas_print(" elr="); nas_hex(frame->elr_el1);
        nas_print(" far="); nas_hex(far);
        nas_print(" sp="); nas_hex(frame->sp_el0);
        nas_print(" access="); nas_hex(esr & 0x7f);
        nas_print(" user_writable=");
        nas_dec(nas_mmu_user_writable(far) ? 1 : 0);
        nas_print("\n");
        nas_fail("unexpected AArch64 EL0 synchronous exception");
    }
    /* ARMv8-A SVC saves NextInstrAddr() to ELR_EL1 in hardware.
     * Unlike RISC-V ECALL/sepc, advancing PC again SKIPS the instruction
     * after SVC. The native hello ELF prints successfully but misses
     * 'cmp x0,#6', branching with stale NZCV and exiting nonzero.
     * Save/use the hardware-provided post-SVC ELR unchanged. */
    uint64_t call[15] = {frame->x[8],frame->x[0],frame->x[1],
        frame->x[2],frame->x[3],frame->x[4],frame->x[5],frame->elr_el1};
    /* One switch re-enables syscall tracing without noise in normal boots.
     * Fault diagnostics above remain unconditional. */
#ifdef NASEED_TRACE_ARCH_SYSCALL
    nas_print("[ARM64 SVC] nr="); nas_dec(frame->x[8]);
    nas_print(" pc="); nas_hex(frame->elr_el1);
    nas_print(" x0="); nas_hex(frame->x[0]);
    nas_print(" lr="); nas_hex(frame->x[30]);
    nas_print("\n");
#endif
    live=frame;
    uint64_t result=nas_ptrace_syscall_dispatch(call);
    frame->x[0]=result;
    nas_signal_user_return((uint64_t *)(void *)frame);
#ifdef NASEED_TRACE_ARCH_SYSCALL
    nas_print("[ARM64 SVC RETURN] nr="); nas_dec(call[0]);
    nas_print(" result="); nas_hex(frame->x[0]);
    nas_print(" pc="); nas_hex(frame->elr_el1);
    nas_print("\n");
#endif
    live=0;
}
void nas_arch_restore_cpu(const struct nas_task *task) {
    if (!nas_task_kernel_stack_ok(task)) nas_fail("ARM64 switching to invalid stack");
    nas_arm_entry_stack_top = task->kernel_stack_top;
    nas_mmu_switch_root(task->root_phys);
    uint64_t tls=nas_task_tls_base(task);
    __asm__ volatile("msr tpidr_el0,%0" : : "r"(tls) : "memory");
    nas_arm_load_simd(task->arch.simd_regs);
}
uint64_t nas_arch_signal_user_sp(void) {
    if(!live)nas_fail("ARM64 signal return outside live trap");
    return live->sp_el0;
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
