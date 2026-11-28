#ifndef NASEED_ARCH_SIGNAL_FRAME_COMMON_H
#define NASEED_ARCH_SIGNAL_FRAME_COMMON_H
#include "../task.h"
#include "../task/signal.h"
#include "../mm/usercopy.h"
#include "arch.h"
#include "abi.h"
#include "mmu.h"

/* Only the kernel-owned snapshot lifecycle is common. Native frame layout,
 * FP serialization, stack alignment and restorer PC validation stay with
 * the ISA. No privileged context is imported from the user frame. */
static inline void nas_signal_frame_commit(struct nas_task *t,
    const struct nas_arch_context *saved,uint64_t sp,unsigned signo,
    const uint64_t action[4]) {
    t->signal_saved_arch=*saved;
    t->signal_saved_mask=t->blocked_signals;
    t->signal_frame_sp=sp;
    t->signal_frame_active=true;
    nas_signal_handler_enter(t,signo,action);
}
static inline uint64_t nas_signal_frame_restore(struct nas_task *t,uint64_t *frame) {
    t->arch=t->signal_saved_arch;
    t->blocked_signals=t->signal_saved_mask;
    t->signal_frame_active=false;
    t->signal_frame_sp=0;
    nas_arch_restore_cpu(t);
    nas_arch_load_user_frame(t,frame);
    return nas_arch_saved_return(t);
}
#if !defined(__x86_64__)
/* Fixed RX restorer in each new MM. ARM needs explicit I-cache sync;
 * the RV64 copy path retains its existing synchronization contract. */
static inline bool nas_signal_map_restorer(const uint32_t *code,uint64_t bytes,
                                           bool sync_exec) {
    if(!nas_mmu_map_prot_page(NAS_USER_SIGNAL_TRAMPOLINE,5U))return false;
    if(!nas_user_copy_to_mapped(NAS_USER_SIGNAL_TRAMPOLINE,
                                (const uint8_t *)code,bytes) ||
       (sync_exec && !nas_mmu_sync_user_exec(NAS_USER_SIGNAL_TRAMPOLINE,bytes))) {
        (void)nas_mmu_unmap_page(NAS_USER_SIGNAL_TRAMPOLINE);
        return false;
    }
    return true;
}
#endif
#endif
