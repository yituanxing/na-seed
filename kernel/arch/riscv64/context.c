#include <stdint.h>
#include <stdbool.h>
#include "../arch.h"
#include "../mmu.h"
#include "../../task.h"
#include "../../syscall/dispatch.h"
#include "../../task/signal.h"
#include "../../panic.h"
#include "../../drivers/serial.h"
#include "../../lib/string.h"
#include "context.h"
extern void nas_rv_timer_handle(struct nas_rv_trap_frame *frame);
extern void nas_rv_save_fp(uint64_t *);
extern void nas_rv_restore_fp(const uint64_t *);
static struct nas_rv_trap_frame *live;
/* Used by first user entry and by an execve that replaces a process image. */
uint64_t nas_rv_entry_stack_top;
uint64_t nas_rv_return_stack(const struct nas_rv_trap_frame *source) {
    const struct nas_task *task=nas_current_task;
    if (!nas_task_kernel_stack_ok(task))
        nas_fail("RV64 invalid task kernel stack at sret");
    uint64_t dest=task->kernel_stack_top-sizeof(*source);
    if (dest != (uint64_t)(uintptr_t)source)
        memcpy((void *)(uintptr_t)dest, source, sizeof(*source));
    return dest;
}

/* Architecture entry translates the full hardware register image to the
 * established 8-word Linux syscall contract used by the x86 kernel. */
void nas_rv_full_trap_c(struct nas_rv_trap_frame *frame) {
    uint64_t scause, stval;
    __asm__ volatile("csrr %0, scause" : "=r"(scause));
    __asm__ volatile("csrr %0, stval" : "=r"(stval));
    /* STIP is delegated by OpenSBI. It is a genuine asynchronous
     * user interrupt, not an ECALL: sepc points to the interrupted
     * instruction and MUST NOT be incremented. */
    if(scause == (UINT64_C(1)<<63 | 5U) && !(frame->sstatus & 0x100U)) {
        live=frame;
        nas_rv_timer_handle(frame);
        nas_signal_user_return((uint64_t *)(void *)frame);
        live=0;
        return;
    }
    if(scause != 8U || (frame->sstatus & 0x100U)) {
        nas_print("[RV64 FAULT] scause=");nas_hex(scause);
        nas_print(" sepc=");nas_hex(frame->sepc);
        nas_print(" stval=");nas_hex(stval);
        nas_print(" tp=");nas_hex(frame->x[4]);
        nas_print(" sp=");nas_hex(frame->x[2]);
        nas_print("\n");
        nas_fail("unexpected RV64 user trap (not U-mode ECALL)");
    }
    frame->sepc += 4; /* ECALL is always an uncompressed 4-byte instruction. */
    uint64_t call[15]={frame->x[17],frame->x[10],frame->x[11],
        frame->x[12],frame->x[13],frame->x[14],frame->x[15],frame->sepc};
    live=frame;
    uint64_t result=syscall_dispatch(call);
    /* Scheduler may have replaced *frame* with another task's snapshot. */
    frame->x[10]=result;
    nas_signal_user_return((uint64_t *)(void *)frame);
    live=0;
}
void nas_arch_restore_cpu(const struct nas_task *task) {
    if (!nas_task_kernel_stack_ok(task)) nas_fail("RV64 switching to invalid stack");
    nas_rv_entry_stack_top = task->kernel_stack_top;
    nas_mmu_switch_root(task->root_phys);
    if(live) live->x[4]=nas_task_tls_base(task);
    nas_rv_restore_fp(task->arch.fp_regs);
}
uint64_t nas_arch_signal_user_sp(void) {
    if(!live)nas_fail("RV64 signal return outside live trap");
    return live->x[2];
}
void nas_arch_save_fpu(struct nas_task *task) {
    nas_rv_save_fp(task->arch.fp_regs);
}
void nas_arch_save_user_frame(struct nas_task *task,const uint64_t *frame) {
    (void)frame;
    if(!live) nas_fail("RV64 context capture outside syscall trap");
    for(unsigned i=0;i<32;i++) task->arch.user_regs[i]=live->x[i];
    task->arch.user_pc=live->sepc;
    task->arch.user_sp=live->x[2];
    task->arch.user_status=live->sstatus;
    task->arch.user_tls_base=live->x[4];
}
void nas_arch_set_saved_return(struct nas_task *task,uint64_t value) {
    task->arch.user_regs[10]=value; /* Linux RV64 a0 */
}
uint64_t nas_arch_saved_return(const struct nas_task *task) {
    return task->arch.user_regs[10];
}
void nas_arch_load_user_frame(const struct nas_task *task,uint64_t *frame) {
    (void)frame;
    if(!live) nas_fail("RV64 context restore outside syscall trap");
    for(unsigned i=0;i<32;i++) live->x[i]=task->arch.user_regs[i];
    live->sepc=task->arch.user_pc;
    live->sstatus=task->arch.user_status;
    live->x[4]=nas_task_tls_base(task);
}
