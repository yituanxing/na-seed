#ifndef NASEED_ARCH_TRAP_COMMON_H
#define NASEED_ARCH_TRAP_COMMON_H
#include <stdint.h>
#include "../task/ptrace.h"
#include "../task/sched.h"
#include "../task/signal.h"

/* RV64 and ARM64 have already captured their native frame and FPU before
 * this call. A stopped tracee resumes its own frame; an untraced fatal
 * exception exits the group. The ISA selects the saved return register. */
static inline void nas_arch_finish_user_exception(struct nas_task *tracee,
    unsigned signo,int32_t code,uint64_t pc,uint64_t sp,uint64_t address,
    uint64_t *frame,uint64_t *return_reg) {
    if(!nas_ptrace_user_exception(tracee,signo,code,pc,sp,address))
        nas_sched_thread_group_exit(0,signo);
    *return_reg=nas_task_schedule(frame);
    nas_signal_user_return(frame);
}
#endif
