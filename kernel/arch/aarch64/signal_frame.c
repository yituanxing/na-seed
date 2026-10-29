/* Native aarch64 Linux signal-frame ABI. Shared Signal Core owns masks,
 * dispositions and selection; this provider only maps native userspace
 * contexts, installs the restorer and restores a checked CPU snapshot. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../../errno.h"
#include "../../task.h"
#include "../../task/signal.h"
#include "../../mm/usercopy.h"
#include "../arch.h"
#include "../abi.h"
#include "../mmu.h"
#include "../../panic.h"
#include "context.h"

/* Linux arm64 uapi sigcontext: 31 GPR, SP/PC/PSTATE, then 4KiB
 * aligned extension buffer with FPSIMD_MAGIC, size=528 and null terminator. */
struct arm_sigcontext {
    uint64_t fault_address,regs[31],sp,pc,pstate;
    uint8_t reserved[4096] __attribute__((aligned(16)));
};
struct arm_ucontext {
    uint64_t flags,link;
    struct {uint64_t sp;uint32_t flags,pad;uint64_t size;} stack;
    uint64_t sigmask;
    uint8_t unused[120];
    struct arm_sigcontext mc;
};
struct arm_sigframe {uint32_t info[32];struct arm_ucontext uc;};
struct arm_fpsimd {
    uint32_t magic,size,fpsr,fpcr;
    uint8_t regs[512] __attribute__((aligned(16)));
};
_Static_assert(offsetof(struct arm_sigcontext,reserved)==288,
               "Linux ARM64 FPSIMD reserved offset");
_Static_assert(sizeof(struct arm_sigcontext)==4384,
               "Linux ARM64 sigcontext");
_Static_assert(offsetof(struct arm_ucontext,mc)==176,
               "Linux ARM64 uc_mcontext");
_Static_assert(sizeof(struct arm_sigframe)==4688,
               "Linux ARM64 rt_sigframe");
_Static_assert(sizeof(struct arm_fpsimd)==528,
               "Linux ARM64 fpsimd_context");
#define NAS_SIGRETURN_TRAMPOLINE NAS_USER_SIGNAL_TRAMPOLINE
bool nas_arch_signal_install_handler(uint64_t *opaque,unsigned signo,
                                const uint64_t *action) {
    struct nas_arm_trap_frame *fr=(struct nas_arm_trap_frame *)(void *)opaque;
    struct nas_task *t=nas_current_task;
    uint64_t handler=action[0];
    if(!handler || handler>=USER_LIMIT || (handler & 3U))return false;
    nas_arch_save_user_frame(t,opaque);
    nas_arch_save_fpu(t);
    struct nas_arch_context saved=t->arch;
    if(fr->sp_el0<sizeof(struct arm_sigframe)+32 ||
       fr->sp_el0>=USER_LIMIT)return false;
    uint64_t sp=(fr->sp_el0-sizeof(struct arm_sigframe))&~UINT64_C(15);
    struct arm_sigframe sf={0};
    sf.info[0]=(uint32_t)signo;
    sf.uc.sigmask=t->blocked_signals;
    for(unsigned i=0;i<31;i++)sf.uc.mc.regs[i]=saved.user_regs[i];
    sf.uc.mc.sp=saved.user_sp;
    sf.uc.mc.pc=saved.user_elr;
    sf.uc.mc.pstate=saved.user_spsr;
    struct arm_fpsimd *fp=(struct arm_fpsimd *)(void *)sf.uc.mc.reserved;
    fp->magic=0x46508001U;
    fp->size=sizeof(*fp);
    fp->fpsr=(uint32_t)saved.fpsr;
    fp->fpcr=(uint32_t)saved.fpcr;
    for(unsigned i=0;i<512;i++)fp->regs[i]=saved.simd_regs[i];
    if(!nas_user_write_bytes(sp,(const uint8_t *)&sf,sizeof(sf)))return false;
    t->signal_saved_arch=saved;
    t->signal_saved_mask=t->blocked_signals;
    t->signal_frame_sp=sp;
    t->signal_frame_active=true;
    nas_signal_handler_enter(t,signo,action);
    fr->sp_el0=sp;
    fr->x[30]=NAS_SIGRETURN_TRAMPOLINE; /* Linux rt_sigreturn via RX page */
    fr->x[0]=signo;
    fr->x[1]=sp+offsetof(struct arm_sigframe,info);
    fr->x[2]=sp+offsetof(struct arm_sigframe,uc);
    fr->elr_el1=handler;
    return true;
}

uint64_t nas_arch_signal_sigreturn(uint64_t *frame) {
    struct nas_task *t=nas_current_task;
    if(!t->signal_frame_active ||
       nas_arch_signal_user_sp()!=t->signal_frame_sp)
        return (uint64_t)-EFAULT;
    t->arch=t->signal_saved_arch;
    t->blocked_signals=t->signal_saved_mask;
    t->signal_frame_active=false;
    t->signal_frame_sp=0;
    nas_arch_restore_cpu(t);
    nas_arch_load_user_frame(t,frame);
    return nas_arch_saved_return(t);

}
bool nas_arch_signal_install_trampoline(void) {
    /* Fixed RX helper mapped afresh into each newly constructed user MMU
     * tree. The syscall number and ISA instruction encoding are native,
     * while Task-level entry/exec ownership remains in the common core. */
    static const uint32_t insns[]={
        0xd2801168U, /* movz x8,#139: Linux rt_sigreturn */
        0xd4000001U, /* svc #0 */
        0xd4200000U, /* brk #0: unexpected return */
    };
    if(!nas_mmu_map_prot_page(NAS_SIGRETURN_TRAMPOLINE,5U))
        return false;
    if(!nas_user_copy_to_mapped(NAS_SIGRETURN_TRAMPOLINE,
                               (const uint8_t *)insns,sizeof(insns)) ||
       !nas_mmu_sync_user_exec(NAS_SIGRETURN_TRAMPOLINE,sizeof(insns))) {
        (void)nas_mmu_unmap_page(NAS_SIGRETURN_TRAMPOLINE);
        return false;
    }
    return true;
}
