#include <stdint.h>
#include <stdbool.h>
#include "../arch.h"
#include "../mmu.h"
#include "../../task.h"
#include "../../syscall/dispatch.h"
#include "../../panic.h"
#include "../../drivers/serial.h"
#include "context.h"
extern void nas_rv_save_fp(uint64_t *);
extern void nas_rv_restore_fp(const uint64_t *);
static struct nas_rv_trap_frame *live;
/* Architecture entry translates the full hardware register image to the
 * established 8-word Linux syscall contract used by the x86 kernel. */
void nas_rv_full_trap_c(struct nas_rv_trap_frame *frame) {
    uint64_t scause;
    __asm__ volatile("csrr %0, scause" : "=r"(scause));
    if(scause != 8U || (frame->sstatus & 0x100U)) {
        nas_print("[RV64 FAULT] scause=");nas_hex(scause);
        nas_print(" sepc=");nas_hex(frame->sepc);nas_print("\n");
        nas_fail("unexpected RV64 user trap (not U-mode ECALL)");
    }
    frame->sepc += 4; /* ECALL is always an uncompressed 4-byte instruction. */
    uint64_t call[15]={frame->x[17],frame->x[10],frame->x[11],
        frame->x[12],frame->x[13],frame->x[14],frame->x[15],frame->sepc};
    live=frame;
    uint64_t result=syscall_dispatch(call);
    /* Scheduler may have replaced *frame* with another task's snapshot. */
    frame->x[10]=result;
    live=0;
}
void nas_arch_restore_cpu(const struct nas_task *task) {
    nas_mmu_switch_root(task->root_phys);
    if(live) live->x[4]=nas_task_tls_base(task);
    nas_rv_restore_fp(task->arch.fp_regs);
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
