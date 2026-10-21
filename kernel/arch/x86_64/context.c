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
