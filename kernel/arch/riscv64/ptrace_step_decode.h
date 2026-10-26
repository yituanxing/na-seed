#ifndef NASEED_RV64_PTRACE_STEP_DECODE_H
#define NASEED_RV64_PTRACE_STEP_DECODE_H
#include <stdint.h>
#include <stdbool.h>
/* Decode the next instruction address from an immutable stopped Task GPR
 * snapshot. RV64 has no common native single-step bit: the kernel plants
 * one private-VM temporary C.EBREAK/EBREAK at the *actual* successor.
 * No guesses for unknown control opcodes or a self-loop. */
static inline int64_t nas_rv_step_sext(uint64_t value,unsigned bits) {
    return ((int64_t)(value<<(64U-bits)))>>(64U-bits);
}
static inline bool nas_rv_step_next_pc(uint64_t pc,uint32_t insn,
                                      const uint64_t regs[32],uint64_t *next) {
    if(!regs || !next || (pc&1U))return false;
    uint64_t dst=pc;
    if((insn&3U)!=3U) {
        uint16_t c=(uint16_t)insn;
        unsigned quadrant=c&3U,funct3=(c>>13)&7U;
        dst=pc+2;
        if(quadrant==1U && funct3==5U) { /* C.J */
            uint32_t imm=((c>>12&1U)<<11)|((c>>11&1U)<<4)|
                ((c>>9&3U)<<8)|((c>>8&1U)<<10)|
                ((c>>7&1U)<<6)|((c>>6&1U)<<7)|
                ((c>>3&7U)<<1)|((c>>2&1U)<<5);
            dst=pc+(uint64_t)nas_rv_step_sext(imm,12);
        } else if(quadrant==1U && (funct3==6U || funct3==7U)) {
            /* C.BEQZ / C.BNEZ use a compact rs1' in x8..x15. */
            unsigned rs=((c>>7)&7U)+8U;
            uint32_t imm=((c>>12&1U)<<8)|((c>>10&3U)<<3)|
                ((c>>5&3U)<<6)|((c>>3&3U)<<1)|((c>>2&1U)<<5);
            bool take=funct3==6U ? regs[rs]==0 : regs[rs]!=0;
            if(take)dst=pc+(uint64_t)nas_rv_step_sext(imm,9);
        } else if(quadrant==2U && funct3==4U && ((c>>2)&31U)==0) {
            /* C.JR/C.JALR: rs1=0 indicates reserved or C.EBREAK. */
            unsigned rs1=(c>>7)&31U;
            if(!rs1)return false;
            dst=regs[rs1]&~UINT64_C(1);
        } else if(quadrant==3U || c==0)
            return false;
    } else {
        uint32_t major=insn&0x7fU;
        dst=pc+4;
        if(major==0x63U) { /* B-type branch, signed and unsigned */
            unsigned a=(insn>>15)&31U,b=(insn>>20)&31U,op=(insn>>12)&7U;
            uint32_t imm=((insn>>31&1U)<<12)|((insn>>7&1U)<<11)|
                ((insn>>25&63U)<<5)|((insn>>8&15U)<<1);
            bool take;
            switch(op) {
            case 0:take=regs[a]==regs[b];break;
            case 1:take=regs[a]!=regs[b];break;
            case 4:take=(int64_t)regs[a]<(int64_t)regs[b];break;
            case 5:take=(int64_t)regs[a]>=(int64_t)regs[b];break;
            case 6:take=regs[a]<regs[b];break;
            case 7:take=regs[a]>=regs[b];break;
            default:return false;
            }
            if(take)dst=pc+(uint64_t)nas_rv_step_sext(imm,13);
        } else if(major==0x6fU) { /* JAL */
            uint32_t imm=((insn>>31&1U)<<20)|((insn>>12&255U)<<12)|
                ((insn>>20&1U)<<11)|((insn>>21&1023U)<<1);
            dst=pc+(uint64_t)nas_rv_step_sext(imm,21);
        } else if(major==0x67U) { /* JALR */
            if(((insn>>12)&7U)!=0U)return false;
            unsigned rs1=(insn>>15)&31U;
            dst=(regs[rs1]+(uint64_t)nas_rv_step_sext(insn>>20,12))&~UINT64_C(1);
        } else if(major==0x73U && insn!=0x00000073U)
            return false; /* CSR/privileged/EBREAK are not guessed */
    }
    if(dst==pc || (dst&1U) || dst>=UINT64_C(0x0000800000000000))
        return false;
    *next=dst;
    return true;
}
#endif
