#include "../arch.h"
#include "../../task.h"
#include "../../task/signal.h"
#include "../../panic.h"
#include "irq.h"
extern void nas_print(const char *);
extern void nas_dec(uint64_t);
extern void nas_hex(uint64_t);
extern void nas_arch_x86_set_kernel_stack(uint64_t);
void nas_arch_restore_cpu(const struct nas_task *task) {
    if (!nas_task_kernel_stack_ok(task)) nas_arch_halt();
    nas_arch_x86_set_kernel_stack(task->kernel_stack_top);
    syscall_user_rsp=task->arch.resume_user_rsp;
    nas_x86_syscall_return_rip=task->arch.resume_user_rip;
    nas_x86_syscall_return_flags=task->arch.resume_user_rflags;
    nas_arch_switch_page_root(nas_task_page_root(task));
    nas_arch_set_user_tls(nas_task_tls_base(task));
    __asm__ volatile("fxrstor64 %0" : : "m"(task->arch.fpu_state) : "memory");
}
uint64_t nas_arch_signal_user_sp(void) { return syscall_user_rsp; }
void nas_arch_save_fpu(struct nas_task *task) {
    __asm__ volatile("fxsave64 %0" : "=m"(task->arch.fpu_state) : : "memory");
}
void nas_arch_save_user_frame(struct nas_task *task, const uint64_t *frame) {
    for (unsigned i=0;i<15;i++) task->arch.resume_regs[i] = frame[i];
    task->arch.resume_user_rsp = syscall_user_rsp;
    /* After an rt_sigreturn (or a ptrace stop resumed in the original
     * native syscall C frame), frame[7]/frame[8] are the user's restored
     * RCX/R11 registers, not necessarily the kernel's IRET RIP/RFLAGS.
     * The syscall-entry assembly and context-restore owner explicitly
     * maintain these return registers independently. */
    task->arch.resume_user_rip = nas_x86_syscall_return_rip;
    task->arch.resume_user_rflags = nas_x86_syscall_return_flags;
    if(task->arch.resume_user_rip<4096U) {
        nas_print("[X86CTX SAVE_BAD] pid=");nas_dec(task->pid);
        nas_print(" global_pc=");nas_hex(nas_x86_syscall_return_rip);
        nas_print(" raw_rcx=");nas_hex(frame[7]);
        nas_print(" raw_r11=");nas_hex(frame[8]);nas_print("\n");
    }
}
void nas_arch_set_saved_return(struct nas_task *task, uint64_t value) {
    task->arch.resume_regs[0] = value;
}
uint64_t nas_arch_saved_return(const struct nas_task *task) {
    return task->arch.resume_regs[0];
}
void nas_arch_load_user_frame(const struct nas_task *task, uint64_t *frame) {
    for (unsigned i=0;i<15;i++) frame[i] = task->arch.resume_regs[i];
    if(task->arch.resume_user_rip<4096U) {
        nas_print("[X86CTX LOAD_BAD] pid=");nas_dec(task->pid);
        nas_print(" rip=");nas_hex(task->arch.resume_user_rip);
        nas_print(" frame_rcx=");nas_hex(frame[7]);nas_print("\n");
    }
    syscall_user_rsp = task->arch.resume_user_rsp;
    nas_x86_syscall_return_rip = task->arch.resume_user_rip;
    nas_x86_syscall_return_flags = task->arch.resume_user_rflags;
}

/* First dispatch starts on a private bootstrap stack, then IRET resumes
 * exactly the saved Linux user-register context on the Task entry stack. */
void nas_arch_pin_live_trap(struct nas_task *t) {
    if(!t)nas_fail("x86 native park missing Task");
}
void nas_arch_bind_live_trap(const struct nas_task *t) {
    if(t && t->native_trap_frame)
        nas_arch_load_user_frame(t,(uint64_t *)(uintptr_t)t->native_trap_frame);
}
extern _Noreturn void nas_x86_user_resume_frame(uint64_t *);
_Noreturn void nas_arch_resume_saved_user(void) {
    struct nas_task *t=nas_current_task;
    if(!t || !nas_task_kernel_stack_ok(t))nas_fail("x86 native resume invalid Task");
    uint64_t *frame=(uint64_t *)(uintptr_t)(t->kernel_stack_top-160U);
    for(unsigned i=0;i<15;i++)frame[i]=t->arch.resume_regs[i];
    frame[15]=t->arch.resume_user_rip;frame[16]=0x33;
    frame[17]=t->arch.resume_user_rflags;
    frame[18]=t->arch.resume_user_rsp;frame[19]=0x2b;
    nas_signal_user_return(frame);
    t->kernel_exec.cpu.saved_sp=0;
    nas_x86_user_resume_frame(frame);
}
