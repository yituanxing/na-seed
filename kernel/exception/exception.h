#ifndef NASEED_EXCEPTION_EXCEPTION_H
#define NASEED_EXCEPTION_EXCEPTION_H
#include <stdint.h>
#include <stdbool.h>
struct nas_mm;
/* ISA trap decoding is separate from MM policy, scheduling and IRQ delivery.
 * IRQs are asynchronous events; they must never be treated as memory faults. */
enum nas_exception_kind {
    NAS_EXC_OTHER=0, NAS_EXC_IRQ, NAS_EXC_SYSCALL, NAS_EXC_MEMORY,
    NAS_EXC_BREAKPOINT, NAS_EXC_SINGLE_STEP, NAS_EXC_ILLEGAL,
    NAS_EXC_ALIGNMENT, NAS_EXC_BUS, NAS_EXC_ARITHMETIC,
    NAS_EXC_BOUNDS, NAS_EXC_PROTECTION
};
enum nas_exception_access {NAS_EXC_READ=1, NAS_EXC_WRITE=2, NAS_EXC_EXEC=4};
struct nas_exception {
    enum nas_exception_kind kind;
    bool user,present;
    unsigned access;
    uint64_t pc,addr;
    int32_t arithmetic_code; /* FPE_INTDIV/FPE_INTOVF, otherwise generic */
};
enum nas_exception_action {NAS_EXC_UNHANDLED=0,NAS_EXC_RETRY,NAS_EXC_SIGNAL};
struct nas_exception_outcome {
    enum nas_exception_action action;
    unsigned signo;
    int32_t si_code;
};
/* CPU bitfield translation is pure C: all three decoders have host tests. */
struct nas_exception nas_exception_x86(unsigned vector,uint64_t error,
                                       uint64_t cr2,uint64_t pc,uint64_t cs);
struct nas_exception nas_exception_rv64(uint64_t scause,uint64_t sstatus,
                                        uint64_t stval,uint64_t sepc);
struct nas_exception nas_exception_arm64(uint64_t esr,uint64_t spsr,
                                         uint64_t far,uint64_t elr);
/* Common synchronous exception policy: MM may repair a fault and retry.
 * Caller owns ISA return-frame, ptrace and user-signal delivery semantics. */
struct nas_exception_outcome nas_exception_dispatch(const struct nas_exception *,
                                                    struct nas_mm *);
#endif
