/* Native riscv64 Linux signal-frame ABI. Shared Signal Core owns masks,
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
#include "../mmu.h"
#include "../../panic.h"
#include "context.h"

/* Linux arch/riscv/include/uapi/asm/{ptrace,sigcontext,ucontext}.h.
 * Native rt_sigframe is [siginfo:128][ucontext:960], 16-byte aligned.
 * LP64D sigcontext starts at uc+176; the FP union reserves 528 bytes
 * for future extensions even without vector support. */
struct rv_sigcontext {
    uint64_t pc,regs[31];
    struct {
        uint64_t fp[32];
        uint32_t fcsr;
        uint8_t reserved[528-32*8-4];
    } __attribute__((aligned(16))) fp;
};
struct rv_ucontext {
    uint64_t flags,link;
    struct {uint64_t sp;uint32_t flags,pad;uint64_t size;} stack;
    uint64_t sigmask;
    uint8_t unused[120];
    struct rv_sigcontext mc;
};
struct rv_sigframe {uint32_t info[32];struct rv_ucontext uc;};
_Static_assert(sizeof(struct rv_sigcontext)==784,"Linux RV64 sigcontext");
_Static_assert(offsetof(struct rv_ucontext,mc)==176,"Linux RV64 uc_mcontext");
_Static_assert(sizeof(struct rv_ucontext)==960,"Linux RV64 ucontext");
_Static_assert(sizeof(struct rv_sigframe)==1088,"Linux RV64 rt_sigframe");
/* A tiny RX vDSO equivalent, mapped into EACH process address space on
 * exec and initial boot. RA never points to a non-executable stack page. */
#define NAS_SIGRETURN_TRAMPOLINE UINT64_C(0x6ff00000)
bool nas_arch_signal_install_handler(uint64_t *opaque,unsigned signo,
                               const uint64_t *action) {
    struct nas_rv_trap_frame *fr=(struct nas_rv_trap_frame *)(void *)opaque;
    struct nas_task *t=nas_current_task;
    uint64_t handler=action[0];
    if(!handler || handler>=USER_LIMIT || (handler & 1U))return false;
    nas_arch_save_user_frame(t,opaque);
    nas_arch_save_fpu(t);
    struct nas_arch_context saved=t->arch;
    if(fr->x[2]<sizeof(struct rv_sigframe)+32 ||
       fr->x[2]>=USER_LIMIT)return false;
    uint64_t sp=(fr->x[2]-sizeof(struct rv_sigframe))&~UINT64_C(15);
    struct rv_sigframe sf={0};
    sf.info[0]=(uint32_t)signo;
    sf.uc.sigmask=t->blocked_signals;
    sf.uc.mc.pc=saved.user_pc;
    for(unsigned i=1;i<32;i++)sf.uc.mc.regs[i-1]=saved.user_regs[i];
    for(unsigned i=0;i<32;i++)sf.uc.mc.fp.fp[i]=saved.fp_regs[i];
    sf.uc.mc.fp.fcsr=(uint32_t)saved.fp_fcsr;
    if(!nas_user_write_bytes(sp,(const uint8_t *)&sf,sizeof(sf)))return false;
    t->signal_saved_arch=saved;
    t->signal_saved_mask=t->blocked_signals;
    t->signal_frame_sp=sp;
    t->signal_frame_active=true;
    nas_signal_handler_enter(t,signo,action);
    fr->x[2]=sp;
    fr->x[1]=NAS_SIGRETURN_TRAMPOLINE; /* return address, not user stack */
    fr->x[10]=signo;
    fr->x[11]=sp+offsetof(struct rv_sigframe,info);
    fr->x[12]=sp+offsetof(struct rv_sigframe,uc);
    fr->sepc=handler;
    return true;
}

uint64_t nas_arch_signal_sigreturn(uint64_t *frame) {
    struct nas_task *t=nas_current_task;
    /* Dispatch passes an 8-word syscall ABI array, NOT a hardware
     * trapframe. Inspect the live per-ISA user SP instead. */
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
        0x08b00893U, /* addi a7,zero,139: Linux rt_sigreturn */
        0x00000073U, /* ecall */
        0x00100073U, /* ebreak: unexpected return */
    };
    if(!nas_mmu_map_prot_page(NAS_SIGRETURN_TRAMPOLINE,5U))
        return false;
    if(!nas_user_copy_to_mapped(NAS_SIGRETURN_TRAMPOLINE,
                               (const uint8_t *)insns,sizeof(insns))) {
        (void)nas_mmu_unmap_page(NAS_SIGRETURN_TRAMPOLINE);
        return false;
    }
    return true;
}
