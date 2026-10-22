#ifndef NASEED_X86_TIMER_IRQ_H
#define NASEED_X86_TIMER_IRQ_H
#include <stdint.h>
/* C must receive exactly the 15 GPRs saved by timer.S, followed by the
 * hardware CPL3 IRET frame: RIP, CS, RFLAGS, RSP, SS. */
struct nas_x86_user_irq_frame {
    uint64_t regs[15];
    uint64_t rip, cs, rflags, rsp, ss;
};
_Static_assert(sizeof(struct nas_x86_user_irq_frame)==160,"x86 IRQ frame layout");
extern uint64_t nas_x86_syscall_return_rip;
extern uint64_t nas_x86_syscall_return_flags;
void nas_x86_timer_irq(struct nas_x86_user_irq_frame *frame);
#endif
