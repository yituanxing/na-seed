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
void nas_x86_timer_irq(struct nas_x86_user_irq_frame *frame) {
    if ((frame->cs & 3U) != 3U)
        nas_fail("x86 timer interrupted kernel C code before IRQ-safe scheduling");
    struct nas_task *from=nas_current_task;
    if (!from || !nas_task_kernel_stack_ok(from))
        nas_fail("x86 timer without a valid running task");
    irq_ticks++;
    if (from->state == NAS_TASK_RUNNING) {
        for (unsigned i=0;i<15;i++)
            from->arch.resume_regs[i]=frame->regs[i];
        from->arch.resume_user_rip=frame->rip;
        from->arch.resume_user_rflags=frame->rflags;
        from->arch.resume_user_rsp=frame->rsp;
        nas_arch_save_fpu(from);
        if (nas_sched_preempt_select()) {
            struct nas_task *to=nas_current_task;
            static unsigned report_count;
            if (report_count++ < 8) {
                extern void nas_print(const char *);
                extern void nas_dec(uint64_t);
                nas_print("[M28] timer-preempt from=");
                nas_dec(from->pid);
                nas_print(" to=");
                nas_dec(to->pid);
                nas_print("\n");
            }
            /* CPU MMU, TLS, TSS and FPU are now selected for 'to'.
             * Unlike SYSRET, IRET also preserves interrupted RCX/R11. */
            for (unsigned i=0;i<15;i++)
                frame->regs[i]=to->arch.resume_regs[i];
            frame->rip=to->arch.resume_user_rip;
            frame->rflags=to->arch.resume_user_rflags;
            frame->rsp=to->arch.resume_user_rsp;
            frame->cs=0x23U;
            frame->ss=0x1bU;
        }
    }
    /* PIC EOI after all MMU and Task changes; IF remains disabled until IRET. */
    nas_arch_outb(0x20,0x20);
}
