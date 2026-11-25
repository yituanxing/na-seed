#ifndef NASEED_RISCV64_SBI_H
#define NASEED_RISCV64_SBI_H
#include <stdint.h>
/* OpenSBI ecall ABI: a0 is the error/legacy result; a1 is the value.
 * Preserve negative legacy getc results and the SBI SRST no-return rule. */
struct nas_rv_sbi_result { int64_t error, value; };
static inline struct nas_rv_sbi_result nas_rv_sbi_call(
    uint64_t eid,uint64_t fid,uint64_t arg0) {
    register uint64_t a0 __asm__("a0")=arg0;
    register uint64_t a1 __asm__("a1")=0;
    register uint64_t a6 __asm__("a6")=fid;
    register uint64_t a7 __asm__("a7")=eid;
    __asm__ volatile("ecall" : "+r"(a0), "+r"(a1)
                     : "r"(a6), "r"(a7) : "memory");
    return (struct nas_rv_sbi_result){(int64_t)a0,(int64_t)a1};
}
#endif
