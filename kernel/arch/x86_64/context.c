#include "../arch.h"
#include "../../task.h"
void nas_arch_restore_cpu(const struct nas_task *task) {
    nas_arch_switch_page_root(nas_task_page_root(task));
    nas_arch_set_user_tls(nas_task_tls_base(task));
    __asm__ volatile("fxrstor64 %0" : : "m"(task->arch.fpu_state) : "memory");
}
void nas_arch_save_fpu(struct nas_task *task) {
    __asm__ volatile("fxsave64 %0" : "=m"(task->arch.fpu_state) : : "memory");
}
void nas_arch_save_user_frame(struct nas_task *task, const uint64_t *frame) {
    for (unsigned i=0;i<15;i++) task->arch.resume_regs[i] = frame[i];
    task->arch.resume_user_rsp = syscall_user_rsp;
}
void nas_arch_set_saved_return(struct nas_task *task, uint64_t value) {
    task->arch.resume_regs[0] = value;
}
uint64_t nas_arch_saved_return(const struct nas_task *task) {
    return task->arch.resume_regs[0];
}
void nas_arch_load_user_frame(const struct nas_task *task, uint64_t *frame) {
    for (unsigned i=0;i<15;i++) frame[i] = task->arch.resume_regs[i];
    syscall_user_rsp = task->arch.resume_user_rsp;
}
