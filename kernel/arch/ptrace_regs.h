#ifndef NASEED_ARCH_PTRACE_REGS_H
#define NASEED_ARCH_PTRACE_REGS_H
#include <stdint.h>
#include "../task.h"
#include "mmu.h"
/* Linux NT_PRSTATUS register layouts, not internal scheduler layouts.
 * A stopped tracee has an immutable Task snapshot; never inspect another
 * Task's live trap frame or dereference its user pointer. */
static inline unsigned nas_arch_ptrace_gpr_words(const struct nas_task *task,
                                                   uint64_t words[34]) {
    const struct nas_arch_context *c=&task->arch;
#if defined(__x86_64__)
    /* user_regs_struct: r15,r14,r13,r12,rbp,rbx,r11,r10,r9,r8,
     * rax,rcx,rdx,rsi,rdi,orig_rax,rip,cs,rflags,rsp,ss,
     * fs_base,gs_base,ds,es,fs,gs. */
    const unsigned indices[15]={14,13,12,11,10,9,8,4,6,5,0,7,3,2,1};
    for(unsigned i=0;i<15;i++) words[i]=c->resume_regs[indices[i]];
    words[15]=task->ptrace_call[0];
    words[16]=c->resume_user_rip;
    words[17]=0x23;
    words[18]=c->resume_user_rflags;
    words[19]=c->resume_user_rsp;
    words[20]=0x1b;
    words[21]=c->user_fs_base;
    words[22]=0; /* user GS base is not set by NaSeed */
    words[23]=0x1b;words[24]=0x1b;
    words[25]=0x1b;words[26]=0;
    return 27;
#elif defined(__riscv) && __riscv_xlen == 64
    /* Linux user_regs_struct starts pc followed by ra..t6 (x1..x31). */
    words[0]=c->user_pc;
    for(unsigned i=1;i<32;i++)words[i]=c->user_regs[i];
    return 32;
#elif defined(__aarch64__)
    /* struct user_pt_regs: x0..x30, sp, pc, pstate. */
    for(unsigned i=0;i<31;i++)words[i]=c->user_regs[i];
    words[31]=c->user_sp;words[32]=c->user_elr;words[33]=c->user_spsr;
    return 34;
/* Symmetric NT_PRSTATUS writeback to ONLY the stopped Task snapshot.
 * No privileged status/segment bits, no kernel/invalid user PC or stack. */
static inline bool nas_arch_ptrace_set_gpr_words(struct nas_task *task,
                                      const uint64_t words[34],unsigned n) {
    struct nas_arch_context *c=&task->arch;
#if defined(__x86_64__)
    if(n!=27 || words[16]>=USER_LIMIT || words[19]>=USER_LIMIT ||
       words[17]!=0x23 || words[20]!=0x1b ||
       (words[18]&(3ULL<<12)) || !(words[18]&2ULL))
        return false;
    const unsigned indices[15]={14,13,12,11,10,9,8,4,6,5,0,7,3,2,1};
    for(unsigned i=0;i<15;i++) c->resume_regs[indices[i]]=words[i];
    task->ptrace_call[0]=words[15];
    c->resume_user_rip=words[16];
    c->resume_user_rflags=words[18];
    c->resume_user_rsp=words[19];
    /* fsbase is a real user-visible GDB register, not arbitrary SS/CS. */
    if(words[21]>=USER_LIMIT)return false;
    c->user_fs_base=words[21];
    return true;
#elif defined(__riscv) && __riscv_xlen==64
    if(n!=32 || words[0]>=USER_LIMIT || (words[0]&1) ||
       words[2]>=USER_LIMIT)return false;
    c->user_pc=words[0];
    for(unsigned i=1;i<32;i++)c->user_regs[i]=words[i];
    c->user_regs[0]=0;
    c->user_sp=words[2];
    return true;
#elif defined(__aarch64__)
    if(n!=34 || words[31]>=USER_LIMIT || words[32]>=USER_LIMIT ||
       (words[32]&3) || (words[33]&~UINT64_C(0xf0000000)) !=
                            (c->user_spsr&~UINT64_C(0xf0000000)))
        return false;
    for(unsigned i=0;i<31;i++)c->user_regs[i]=words[i];
    c->user_sp=words[31];c->user_elr=words[32];
    c->user_spsr=(c->user_spsr&~UINT64_C(0xf0000000)) |
                 (words[33]&UINT64_C(0xf0000000));
    return true;
#endif
}
#endif
}
#endif
