/* Shared native three-ISA Task address-space copying.
 * The caller (ptrace owner) enforces tracer/stop authorization. Single
 * CPU guarantees no concurrent VM destruction during two-phase updates. */
#include <stdint.h>
#include <stdbool.h>
#include "../errno.h"
#include "../task.h"
#include "../arch/mmu.h"
#include "frame.h"
#include "vm.h"
#include "fault.h"
#include "prot.h"
#include "../arch/user_pt.h"
#include "../lib/string.h"
#include "file_backing.h"
int64_t nas_vm_read_task_bytes(const struct nas_task *task,uint64_t va,
                               uint8_t *dst,uint64_t length) {
    if(!task || task->state==NAS_TASK_FREE || task->state==NAS_TASK_ZOMBIE)
        return -ESRCH;
    const struct nas_mm *mm=task->mm;
    if(!mm || !mm->root_phys)return -ESRCH;
    if(!length)return 0;
    if(!dst || va>=USER_LIMIT || length>USER_LIMIT-va)return -EFAULT;
    uint64_t count=0;
    while(count<length) {
        uint64_t at=va+count,pa=0;
        if(!nas_mm_resolve_user(mm,at,&pa,0)) {
            if(nas_mm_handle_fault((struct nas_mm *)mm,at,NAS_UP_READ,false)!=
               NAS_MM_FAULT_RESOLVED ||
               !nas_mm_resolve_user(mm,at,&pa,0))
                return count?(int64_t)count:-EFAULT;
        }
        uint64_t room=4096ULL-(at&4095ULL);
        uint64_t n=length-count;
        if(n>room)n=room;
        memcpy(dst+count,(const void *)(uintptr_t)(nas_hhdm_offset()+pa),
               (size_t)n);
        count+=n;
    }
    return (int64_t)count;
}

/* The stopped tracee's code mapping is normally RX, NOT writable.
 * Linux PTRACE_POKETEXT must still be able to replace an instruction at
 * that precise mapped private page. HHDM does not change user VMA rights.
 * Do not claim atomicity without validating every page FIRST. */
int64_t nas_vm_write_task_bytes(const struct nas_task *task,uint64_t va,
                                const uint8_t *src,uint64_t length) {
    if(!task || task->state==NAS_TASK_FREE || task->state==NAS_TASK_ZOMBIE)
        return -ESRCH;
    const struct nas_mm *mm=task->mm;
    if(!mm || !mm->root_phys)return -ESRCH;
    if(!length)return 0;
    if(!src || va>=USER_LIMIT || length>USER_LIMIT-va)return -EFAULT;
    for(uint64_t count=0;count<length;) {
        uint64_t at=va+count,pa=0;
        const struct nas_anon_region *v=nas_vma_find(&mm->mmap_vmas,at);
        if(v && v->kind==NAS_VMA_FILE_MUTABLE_SHARED &&
           !(v->prot&NAS_VM_WRITE))return -EFAULT;
        if(!nas_mm_resolve_user(mm,at,&pa,0) &&
           !(v && v->kind==NAS_VMA_FILE_MUTABLE_SHARED &&
             nas_mm_handle_fault((struct nas_mm *)mm,at,
                 NAS_UP_WRITE,false)==NAS_MM_FAULT_RESOLVED &&
             nas_mm_resolve_user(mm,at,&pa,0)))
            return -EFAULT; /* must not leave a partial breakpoint */
        uint64_t n=4096ULL-(at&4095ULL);
        if(n>length-count)n=length-count;
        count+=n;
    }
#if defined(__aarch64__)
    uint64_t ctr=0;
    __asm__ volatile("mrs %0, ctr_el0":"=r"(ctr));
    const uint64_t line=4ULL<<((ctr>>16)&15U);
    if(!line || line>4096)return -EFAULT;
#endif
    /* Separate shared data frames before ANY breakpoint bytes are written.
     * A later OOM can leave newly private PTEs but never half-written user
     * instructions, maintaining ptrace's two-phase data mutation contract. */
    for(uint64_t count=0;count<length;) {
        uint64_t at=va+count;
        const struct nas_anon_region *v=nas_vma_find(&mm->mmap_vmas,at);
        if(v && v->kind==NAS_VMA_FILE_MUTABLE_SHARED) {
            if(nas_fs_mmap_dirty(v->backing)<0)return -EFAULT;
        } else if(!(v && v->kind==NAS_VMA_ANON_SHARED) &&
                  !nas_mm_break_cow((struct nas_mm *)mm,at,0,true))
            return -EFAULT;
        uint64_t n=4096ULL-(at&4095ULL);
        if(n>length-count)n=length-count;
        count+=n;
    }
    for(uint64_t count=0;count<length;) {
        uint64_t at=va+count,pa=0;
        if(!nas_mm_resolve_user(mm,at,&pa,0))
            return -EFAULT; /* single-core: preflight remains valid */
        uint64_t n=4096ULL-(at&4095ULL);
        if(n>length-count)n=length-count;
        uint8_t *dst=(uint8_t *)(uintptr_t)(nas_hhdm_offset()+pa);
        memcpy(dst,src+count,(size_t)n);
#if defined(__aarch64__)
        /* The write was through the kernel HHDM alias, but the tracee
         * will execute through its EL0 alias. Clean data to PoU, then
         * invalidate ALL local I-cache aliases (single-core kernel).
         * Cache-line width comes from CTR_EL0.DminLine, not QEMU's 64B. */
        for(uint64_t p=(uint64_t)(uintptr_t)dst &~(line-1);
            p<(((uint64_t)(uintptr_t)dst+n+line-1)&~(line-1));p+=line)
            __asm__ volatile("dc cvau, %0"::"r"(p):"memory");
        __asm__ volatile("dsb ish; ic iallu; dsb ish; isb":::"memory");
#elif defined(__riscv) && __riscv_xlen==64
        __asm__ volatile("fence rw,rw; fence.i":::"memory");
#else
        __asm__ volatile("":::"memory"); /* x86 coherent I/D cache */
#endif
        count+=n;
    }
    return (int64_t)length;
}
