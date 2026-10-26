#include <assert.h>
#include <stdint.h>
#include "../kernel/arch/riscv64/ptrace_step_decode.h"
int main(void) {
    uint64_t r[32]={0},next=0;
    const uint64_t pc=0x1000;
    assert(nas_rv_step_next_pc(pc,0x00150513U,r,&next)&&next==pc+4); /* addi */
    assert(nas_rv_step_next_pc(pc,0x00000463U,r,&next)&&next==pc+8); /* BEQ */
    assert(nas_rv_step_next_pc(pc,0x00001463U,r,&next)&&next==pc+4); /* BNE */
    r[1]=0x1550;
    assert(nas_rv_step_next_pc(pc,0x00008067U,r,&next)&&next==0x1550); /* JALR */
    assert(nas_rv_step_next_pc(pc,0x008000efU,r,&next)&&next==pc+8); /* JAL */
    assert(nas_rv_step_next_pc(pc,0x0001U,r,&next)&&next==pc+2); /* C.NOP */
    assert(nas_rv_step_next_pc(pc,0xa009U,r,&next)&&next==pc+2); /* C.J +2 */
    assert(nas_rv_step_next_pc(pc,0x8082U,r,&next)&&next==0x1550); /* C.JR ra */
    r[8]=1;
    assert(nas_rv_step_next_pc(pc,0xc001U,r,&next)&&next==pc+2); /* C.BEQZ not taken */
    r[8]=0;
    assert(nas_rv_step_next_pc(pc,0xe001U,r,&next)&&next==pc+2); /* C.BNEZ not taken */
    assert(!nas_rv_step_next_pc(pc,0xa001U,r,&next)); /* self branch */
    assert(!nas_rv_step_next_pc(pc,0x9002U,r,&next)); /* C.EBREAK */
    assert(!nas_rv_step_next_pc(pc,0x00100073U,r,&next)); /* EBREAK */
    assert(!nas_rv_step_next_pc(pc,0x00000000U,r,&next)); /* illegal */
    return 0;
}
