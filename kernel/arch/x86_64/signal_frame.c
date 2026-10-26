/* Native x86_64 Linux signal-frame ABI. Shared Signal Core owns masks,
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
#include "irq.h"

/* Native Linux amd64 struct sigcontext; offsets agree with
 * arch/x86/include/uapi/asm/sigcontext.h. No libc headers in the kernel. */
struct x86_sigcontext {
    uint64_t gpr[18];
    uint16_t cs,gs,fs,ss;
    uint64_t err,trapno,oldmask,cr2,fpstate,reserved[8];
};
struct x86_ucontext {
    uint64_t flags,link;
    struct { uint64_t sp; uint32_t flags,pad; uint64_t size; } stack;
    struct x86_sigcontext sc;
    uint64_t sigmask;
};
struct x86_rt_sigframe {
    uint64_t pretcode;
    struct x86_ucontext uc;
    uint32_t info[32]; /* 128-byte native siginfo_t */
};
_Static_assert(sizeof(struct x86_sigcontext)==256,"Linux x86 sigcontext");
_Static_assert(sizeof(struct x86_ucontext)==304,"Linux x86 ucontext header");
_Static_assert(sizeof(struct x86_rt_sigframe)==440,"Linux x86 rt_sigframe");
_Static_assert(offsetof(struct x86_rt_sigframe,info)==312,"Linux siginfo pointer");
bool nas_arch_signal_install_handler(uint64_t *regs,unsigned signo,
                                const uint64_t *action) {
    const uint64_t flags=action[1],restorer=action[2],handler=action[0];
    if(!(flags & 0x04000000ULL) || !restorer || restorer>=USER_LIMIT ||
       handler>=USER_LIMIT)return false; /* x86-64 SA_RESTORER required */
    struct nas_task *t=nas_current_task;
    struct nas_arch_context save=t->arch;
    for(unsigned i=0;i<15;i++)save.resume_regs[i]=regs[i];
    save.resume_user_rip=regs[15];
    save.resume_user_rflags=regs[17];
    save.resume_user_rsp=regs[18];
    nas_arch_save_fpu(t);
    for(unsigned i=0;i<sizeof(save.fpu_state);i++)
        save.fpu_state[i]=t->arch.fpu_state[i];
    /* Linux AMD64's 128-byte red zone must be preserved for an
     * asynchronous signal even when a register-only leaf is interrupted. */
    uint64_t sp=save.resume_user_rsp;
    if(sp<128+512+440+24 || sp>=USER_LIMIT)return false;
    uint64_t fpaddr=(sp-128-512)&~UINT64_C(15);
    uint64_t frameaddr=((fpaddr-sizeof(struct x86_rt_sigframe))&
                        ~UINT64_C(15))-8ULL;
    struct x86_rt_sigframe uf={0};
    uf.pretcode=restorer;
    uf.uc.flags=0x6; /* UC_SIGCONTEXT_SS | UC_STRICT_RESTORE_SS */
    uf.uc.sigmask=t->blocked_signals;
    uf.uc.sc.gpr[0]=regs[5];  /* r8 */
    uf.uc.sc.gpr[1]=regs[6];  /* r9 */
    uf.uc.sc.gpr[2]=regs[4];  /* r10 */
    uf.uc.sc.gpr[3]=regs[8];  /* r11 */
    uf.uc.sc.gpr[4]=regs[11]; /* r12 */
    uf.uc.sc.gpr[5]=regs[12]; /* r13 */
    uf.uc.sc.gpr[6]=regs[13]; /* r14 */
    uf.uc.sc.gpr[7]=regs[14]; /* r15 */
    uf.uc.sc.gpr[8]=regs[1];  /* di */
    uf.uc.sc.gpr[9]=regs[2];  /* si */
    uf.uc.sc.gpr[10]=regs[10];/* bp */
    uf.uc.sc.gpr[11]=regs[9]; /* bx */
    uf.uc.sc.gpr[12]=regs[3]; /* dx */
    uf.uc.sc.gpr[13]=regs[0]; /* ax */
    uf.uc.sc.gpr[14]=regs[7]; /* cx */
    uf.uc.sc.gpr[15]=regs[18];/* sp */
    uf.uc.sc.gpr[16]=regs[15];/* ip */
    uf.uc.sc.gpr[17]=regs[17];/* flags */
    uf.uc.sc.cs=0x33U;
    uf.uc.sc.ss=0x2bU;
    uf.uc.sc.fpstate=fpaddr;
    uf.info[0]=(uint32_t)signo; /* si_signo */
    /* FP state is a real FXSAVE image from the exact selected task, not
     * another task's stack. Written before modifying any saved CPU state. */
    if(!nas_user_write_bytes(fpaddr,save.fpu_state,512) ||
       !nas_user_write_bytes(frameaddr,(const uint8_t *)&uf,sizeof(uf)))
        return false;
    t->signal_saved_arch=save;
    t->signal_saved_mask=t->blocked_signals;
    t->signal_frame_sp=frameaddr;
    t->signal_frame_active=true;
    nas_signal_handler_enter(t,signo,action);
    regs[0]=0;
    regs[1]=signo;
    regs[2]=frameaddr+offsetof(struct x86_rt_sigframe,info);
    regs[3]=frameaddr+offsetof(struct x86_rt_sigframe,uc);
    regs[15]=handler;
    regs[18]=frameaddr;
    return true;
}

uint64_t nas_arch_signal_sigreturn(uint64_t *frame) {
    struct nas_task *t=nas_current_task;
    if(!t->signal_frame_active ||
       syscall_user_rsp != t->signal_frame_sp+8)
        return (uint64_t)-EFAULT;
    /* The handler's untrusted user frame cannot inject privileged CS,
     * SS or flags into IRET: restore the kernel-held full user snapshot.
     * A future ucontext-mutation implementation will validate each field. */
    t->arch=t->signal_saved_arch;
    t->blocked_signals=t->signal_saved_mask;
    t->signal_frame_active=false;
    t->signal_frame_sp=0;
    nas_arch_restore_cpu(t);
    nas_arch_load_user_frame(t,frame);
    return nas_arch_saved_return(t);

}
bool nas_arch_signal_install_trampoline(void) {
    /* x86-64 libc's SA_RESTORER supplies its executable return path. */
    return true;
}
