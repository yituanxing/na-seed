#include "exception.h"
#include "../mm/mm.h"
#include "../mm/fault.h"
/* Architectures decode raw registers into a shared cause. The shared
 * exception layer decides which specialized subsystem may handle it. */
struct nas_exception nas_exception_x86(unsigned vector,uint64_t error,
                                       uint64_t cr2,uint64_t pc,uint64_t cs) {
    struct nas_exception e={.pc=pc,.addr=cr2,.user=(cs&3U)==3U};
    if(vector==14U){
        e.kind=(error&8U)?NAS_EXC_OTHER:NAS_EXC_MEMORY;
        e.present=(error&1U)!=0;
        e.access=(error&16U)?NAS_EXC_EXEC:((error&2U)?NAS_EXC_WRITE:NAS_EXC_READ);
    } else if(vector==1U)e.kind=NAS_EXC_SINGLE_STEP;
    else if(vector==3U)e.kind=NAS_EXC_BREAKPOINT;
    else if(vector==0U || vector==4U || vector==16U || vector==19U){
        e.kind=NAS_EXC_ARITHMETIC;
        e.arithmetic_code=vector==0U?1:vector==4U?2:0;
    }
    else if(vector==5U)e.kind=NAS_EXC_BOUNDS;
    else if(vector==10U || vector==11U || vector==12U)
        e.kind=NAS_EXC_PROTECTION;
    else if(vector==6U)e.kind=NAS_EXC_ILLEGAL;
    else if(vector==17U)e.kind=NAS_EXC_ALIGNMENT;
    return e;
}
struct nas_exception nas_exception_rv64(uint64_t scause,uint64_t status,
                                        uint64_t stval,uint64_t sepc) {
    struct nas_exception e={.pc=sepc,.addr=stval,.user=(status&0x100U)==0};
    if(scause&(UINT64_C(1)<<63)){e.kind=NAS_EXC_IRQ;return e;}
    if(scause==8U){e.kind=NAS_EXC_SYSCALL;return e;}
    if(scause==12U||scause==13U||scause==15U){
        e.kind=NAS_EXC_MEMORY;
        e.access=scause==12U?NAS_EXC_EXEC:scause==15U?NAS_EXC_WRITE:NAS_EXC_READ;
    } else if(scause==3U)e.kind=NAS_EXC_BREAKPOINT;
    else if(scause==2U)e.kind=NAS_EXC_ILLEGAL;
    else if(scause==0U||scause==4U||scause==6U)e.kind=NAS_EXC_ALIGNMENT;
    else if(scause==1U||scause==5U||scause==7U)e.kind=NAS_EXC_BUS;
    return e;
}
struct nas_exception nas_exception_arm64(uint64_t esr,uint64_t spsr,
                                         uint64_t far,uint64_t elr) {
    unsigned ec=(unsigned)((esr>>26)&63U);
    struct nas_exception e={.pc=elr,.addr=far,.user=(spsr&15U)==0};
    if(ec==0x15U){e.kind=NAS_EXC_SYSCALL;return e;}
    if(ec==0x3cU){e.kind=NAS_EXC_BREAKPOINT;return e;}
    if(ec==0x32U){e.kind=NAS_EXC_SINGLE_STEP;return e;}
    if(ec==0x2cU){e.kind=NAS_EXC_ARITHMETIC;return e;}
    if(ec==0x00U || ec==0x0eU){e.kind=NAS_EXC_ILLEGAL;return e;}
    if(ec==0x20U||ec==0x21U||ec==0x24U||ec==0x25U){
        unsigned fsc=(unsigned)esr&63U;
        e.kind=fsc==0x21U?NAS_EXC_ALIGNMENT:
               fsc<=15U?NAS_EXC_MEMORY:NAS_EXC_BUS;
        e.present=(fsc>=12U&&fsc<=15U);
        e.access=(ec==0x20U||ec==0x21U)?NAS_EXC_EXEC:
                 ((esr&0x40U)?NAS_EXC_WRITE:NAS_EXC_READ);
    } else if(ec==0x22U||ec==0x26U)e.kind=NAS_EXC_ALIGNMENT;
    return e;
}
struct nas_exception_outcome nas_exception_dispatch(const struct nas_exception *e,
                                                     struct nas_mm *mm) {
    struct nas_exception_outcome out={0};
    /* Kernel faults require a future explicit uaccess fixup table; sending
     * a user signal from a kernel-mode fault would hide kernel corruption. */
    if(!e || !e->user)return out;
    /* A faulty EL0/U-mode instruction or data access must terminate that
     * process, never bring down the whole kernel. Debug traps and genuine
     * kernel exceptions remain owned by their dedicated architecture path. */
    if(e->kind==NAS_EXC_ILLEGAL) {
        out.action=NAS_EXC_SIGNAL;out.signo=4;out.si_code=1; /* ILL_ILLOPC */
        return out;
    }
    if(e->kind==NAS_EXC_ARITHMETIC) {
        out.action=NAS_EXC_SIGNAL;out.signo=8; /* SIGFPE */
        out.si_code=e->arithmetic_code; /* INTDIV / INTOVF / generic FP */
        return out;
    }
    if(e->kind==NAS_EXC_BOUNDS || e->kind==NAS_EXC_PROTECTION) {
        out.action=NAS_EXC_SIGNAL;out.signo=11; /* SIGSEGV */
        out.si_code=e->kind==NAS_EXC_BOUNDS?3:0x80; /* BNDERR/SI_KERNEL */
        return out;
    }
    if(e->kind==NAS_EXC_ALIGNMENT || e->kind==NAS_EXC_BUS) {
        out.action=NAS_EXC_SIGNAL;out.signo=7; /* SIGBUS */
        out.si_code=e->kind==NAS_EXC_ALIGNMENT?1:2; /* ADRALN/ADRERR */
        return out;
    }
    if(e->kind==NAS_EXC_MEMORY){
        enum nas_mm_fault_result status=nas_mm_handle_fault(mm,e->addr,e->access,e->present);
        if(status==NAS_MM_FAULT_RESOLVED){out.action=NAS_EXC_RETRY;return out;}
        if(status==NAS_MM_FAULT_MAPERR||status==NAS_MM_FAULT_ACCERR){
            out.action=NAS_EXC_SIGNAL;
            out.signo=11; /* Linux SIGSEGV */
            out.si_code=status==NAS_MM_FAULT_MAPERR?1:2;
        } else if(status==NAS_MM_FAULT_NEEDS_BACKING) {
            /* A file-backed page with no available pager cannot be made
             * present by retrying. POSIX reports an inaccessible backing
             * object as SIGBUS rather than panicking or zero-filling it. */
            out.action=NAS_EXC_SIGNAL;out.signo=7;out.si_code=2;
        } else if(status==NAS_MM_FAULT_OOM) {
            /* Local fault allocation failure is fatal to the user task,
             * not to unrelated kernel threads. No speculative PTE was
             * installed by the MM allocator's rollback path. */
            out.action=NAS_EXC_SIGNAL;out.signo=9;
        }
        /* INVARIANT and unknown MM statuses remain kernel-fatal. */
    }
    return out;
}
