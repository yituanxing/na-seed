/* Compile REAL common exception.c, mm/fault.c and VMA implementation;
 * simulate only physical frame allocation / native PTE hardware.
 * Not a claim that QEMU three-ISA guest fault probes have passed. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/mm/mm.h"
#include "../kernel/mm/fault.h"
#include "../kernel/exception/exception.h"
#include "../kernel/arch/user_pt.h"
#define ROOT 0x3000ULL
#define BASE 0x20010000ULL
#define PAGE 4096ULL
static struct nas_mm space={.users=1,.root_phys=ROOT};
static uint64_t present_va,present_pa;
static unsigned present_access,frames,freed;
static uint8_t dummy_ram[8192];
uint64_t nas_hhdm_offset(void){return (uint64_t)(uintptr_t)dummy_ram;}
uint32_t nas_frame_refs(uint64_t pa){(void)pa;return 1;}
static bool fail_alloc,fail_prepare;
uint64_t nas_frame_alloc(void) {
    if(fail_alloc)return 0;
    return 0x100000ULL+(++frames)*PAGE;
}
bool nas_frame_put(uint64_t pa){assert(pa);freed++;return true;}
bool nas_mm_resolve_user(const struct nas_mm *m,uint64_t addr,
                         uint64_t *pa,unsigned *access) {
    assert(m==&space);
    if(!present_pa||(addr&~(PAGE-1))!=present_va)return false;
    if(pa)*pa=present_pa+(addr&(PAGE-1));
    if(access)*access=present_access;
    return true;
}
bool nas_mmu_prepare_leaf_at(uint64_t root,uint64_t va) {
    assert(root==ROOT && !(va&(PAGE-1)));
    return !fail_prepare;
}
bool nas_mmu_exchange_leaf_at(uint64_t root,uint64_t va,uint64_t newpa,
                              unsigned prot,uint64_t *old) {
    assert(root==ROOT && !(va&(PAGE-1)) && newpa && old);
    *old=present_pa;
    present_va=va;present_pa=newpa;
    present_access=NAS_UP_READ|((prot&2)?NAS_UP_WRITE:0)|
                   ((prot&4)?NAS_UP_EXEC:0);
    return true;
}
/* M88 tests the policy using simulated PTEs; the integrated M101 test
 * links the real MM COW implementation and real usercopy/ptrace callers. */
bool nas_mm_break_cow(struct nas_mm *mm,uint64_t va,
                      unsigned logical_prot,bool preserve_perm) {
    assert(mm==&space);
    (void)preserve_perm;
    uint64_t prior=0;
    return nas_mmu_exchange_leaf_at(ROOT,va&~(PAGE-1),
                                     present_pa,logical_prot,&prior) &&
           prior==present_pa;
}
static void unmap_mock(void){present_pa=present_va=0;present_access=0;}
int main(void) {
    assert(nas_vma_record(&space.mmap_vmas,BASE,2*PAGE,3,
                          NAS_VMA_ANON_PRIVATE,0));
    assert(nas_vma_record(&space.mmap_vmas,BASE+4*PAGE,PAGE,1,
                          NAS_VMA_FILE_PRIVATE_SNAPSHOT,0));
    assert(nas_vma_record(&space.mmap_vmas,BASE+6*PAGE,PAGE,0,
                          NAS_VMA_ANON_PRIVATE,0));
    struct nas_exception x=nas_exception_x86(14,0x6,BASE,0x401000,0x33);
    assert(x.kind==NAS_EXC_MEMORY && x.user && x.access==NAS_EXC_WRITE);
    struct nas_exception_outcome out=nas_exception_dispatch(&x,&space);
    assert(out.action==NAS_EXC_RETRY && present_va==BASE &&
           present_access==(NAS_UP_READ|NAS_UP_WRITE));
    x.addr=BASE+32*PAGE;
    out=nas_exception_dispatch(&x,&space);
    assert(out.action==NAS_EXC_SIGNAL && out.signo==11 && out.si_code==1);
    x.addr=BASE+6*PAGE;
    out=nas_exception_dispatch(&x,&space);
    assert(out.action==NAS_EXC_SIGNAL && out.si_code==2);
    /* A logically writable resident page resolves COW without aliasing. */
    x.addr=BASE;present_access=NAS_UP_READ;
    out=nas_exception_dispatch(&x,&space);
    assert(out.action==NAS_EXC_RETRY &&
           (present_access&NAS_UP_WRITE));
    present_access=NAS_UP_READ|NAS_UP_WRITE;
    x.addr=BASE+4*PAGE;x.access=NAS_EXC_READ;
    assert(nas_mm_handle_fault(&space,x.addr,NAS_UP_READ,false)==NAS_MM_FAULT_NEEDS_BACKING);
    out=nas_exception_dispatch(&x,&space);
    assert(out.action==NAS_EXC_SIGNAL && out.signo==7 && out.si_code==2);
    unmap_mock();
    x.addr=BASE+PAGE;
    fail_alloc=true;
    assert(nas_mm_handle_fault(&space,x.addr,NAS_UP_WRITE,false)==NAS_MM_FAULT_OOM);
    out=nas_exception_dispatch(&x,&space);
    assert(out.action==NAS_EXC_SIGNAL && out.signo==9 && !present_pa);
    fail_alloc=false;fail_prepare=true;
    unsigned before=freed;
    assert(nas_mm_handle_fault(&space,x.addr,NAS_UP_WRITE,false)==NAS_MM_FAULT_OOM);
    assert(freed==before+1 && !present_pa);
    fail_prepare=false;
    /* A hardware fault on an already usable resident PTE is a broken
     * kernel/page-table invariant, NOT a catchable userspace SIGSEGV. */
    present_va=BASE;present_pa=0x200000;present_access=NAS_UP_READ|NAS_UP_WRITE;
    x.addr=BASE;
    assert(nas_mm_handle_fault(&space,x.addr,NAS_UP_WRITE,true)==NAS_MM_FAULT_INVARIANT);
    out=nas_exception_dispatch(&x,&space);
    assert(out.action==NAS_EXC_UNHANDLED);
    unmap_mock();
    x.user=false;
    out=nas_exception_dispatch(&x,&space);
    assert(out.action==NAS_EXC_UNHANDLED && !present_pa);
    struct nas_exception rv=nas_exception_rv64(15,0,BASE+PAGE,0x6000);
    assert(rv.kind==NAS_EXC_MEMORY && rv.user && rv.access==NAS_EXC_WRITE);
    rv=nas_exception_rv64(12,0,BASE,0x6000);
    assert(rv.kind==NAS_EXC_MEMORY && rv.access==NAS_EXC_EXEC);
    rv=nas_exception_rv64(13,0x100,BASE,0x6000);
    assert(rv.kind==NAS_EXC_MEMORY && !rv.user);
    rv=nas_exception_rv64((1ULL<<63)|5,0,0,0);
    assert(rv.kind==NAS_EXC_IRQ);
    rv=nas_exception_rv64(2,0,0,0x6000);
    assert(rv.kind==NAS_EXC_ILLEGAL && rv.user);
    out=nas_exception_dispatch(&rv,&space);
    assert(out.action==NAS_EXC_SIGNAL && out.signo==4 && out.si_code==1);
    rv=nas_exception_rv64(4,0,BASE,0x6000);
    out=nas_exception_dispatch(&rv,&space);
    assert(rv.kind==NAS_EXC_ALIGNMENT && out.action==NAS_EXC_SIGNAL &&
           out.signo==7 && out.si_code==1);
    rv=nas_exception_rv64(5,0,BASE,0x6000);
    out=nas_exception_dispatch(&rv,&space);
    assert(rv.kind==NAS_EXC_BUS && out.action==NAS_EXC_SIGNAL &&
           out.signo==7 && out.si_code==2);
    rv=nas_exception_rv64(2,0x100,0,0x6000);
    assert(rv.kind==NAS_EXC_ILLEGAL && !rv.user &&
           nas_exception_dispatch(&rv,&space).action==NAS_EXC_UNHANDLED);
    struct nas_exception arm=nas_exception_arm64((0x24ULL<<26)|0x44,0,
                                                  BASE+PAGE,0x7000);
    assert(arm.kind==NAS_EXC_MEMORY && arm.user &&
           arm.access==NAS_EXC_WRITE);
    arm=nas_exception_arm64((0x20ULL<<26)|0x0d,0,BASE,0x7000);
    assert(arm.kind==NAS_EXC_MEMORY && arm.access==NAS_EXC_EXEC && arm.present);
    arm=nas_exception_arm64((0x15ULL<<26),0,0,0);
    assert(arm.kind==NAS_EXC_SYSCALL);
    arm=nas_exception_arm64(0,0,0,0x7000);
    out=nas_exception_dispatch(&arm,&space);
    assert(arm.kind==NAS_EXC_ILLEGAL && out.action==NAS_EXC_SIGNAL &&
           out.signo==4 && out.si_code==1);
    arm=nas_exception_arm64((0x24ULL<<26)|0x21,0,BASE,0x7000);
    out=nas_exception_dispatch(&arm,&space);
    assert(arm.kind==NAS_EXC_ALIGNMENT && out.action==NAS_EXC_SIGNAL &&
           out.signo==7 && out.si_code==1);
    arm=nas_exception_arm64((0x24ULL<<26)|0x10,0,BASE,0x7000);
    out=nas_exception_dispatch(&arm,&space);
    assert(arm.kind==NAS_EXC_BUS && out.action==NAS_EXC_SIGNAL &&
           out.signo==7 && out.si_code==2);
    arm=nas_exception_arm64((0x2cULL<<26),0,0,0x7000);
    out=nas_exception_dispatch(&arm,&space);
    assert(arm.kind==NAS_EXC_ARITHMETIC && out.action==NAS_EXC_SIGNAL &&
           out.signo==8 && out.si_code==0);
    arm=nas_exception_arm64(0,5,0,0x7000);
    assert(arm.kind==NAS_EXC_ILLEGAL && !arm.user &&
           nas_exception_dispatch(&arm,&space).action==NAS_EXC_UNHANDLED);
    x=nas_exception_x86(14,0x14,BASE,0,0x33);
    assert(x.access==NAS_EXC_EXEC && !x.present);
    assert(nas_exception_x86(1,0,0,0,0x33).kind==NAS_EXC_SINGLE_STEP);
    assert(nas_exception_x86(3,0,0,0,0x33).kind==NAS_EXC_BREAKPOINT);
    x=nas_exception_x86(6,0,0,0x401000,0x33);
    out=nas_exception_dispatch(&x,&space);
    assert(x.kind==NAS_EXC_ILLEGAL && out.action==NAS_EXC_SIGNAL &&
           out.signo==4 && out.si_code==1);
    x=nas_exception_x86(17,0,0,0x401000,0x33);
    out=nas_exception_dispatch(&x,&space);
    assert(x.kind==NAS_EXC_ALIGNMENT && out.action==NAS_EXC_SIGNAL &&
           out.signo==7 && out.si_code==1);
    x=nas_exception_x86(6,0,0,0x401000,0x08);
    assert(!x.user && nas_exception_dispatch(&x,&space).action==NAS_EXC_UNHANDLED);
    x=nas_exception_x86(3,0,0,0x401000,0x33);
    assert(nas_exception_dispatch(&x,&space).action==NAS_EXC_UNHANDLED);
    /* Actual x86 0..31 vector meaning, decoded without privileged CPU
     * state. Hardware ISR routing is separately tested at entry.S. */
    static const struct {
        unsigned vector;
        enum nas_exception_kind kind;
        unsigned signo;
        int32_t si_code;
    } user_sync[] = {
        {0,NAS_EXC_ARITHMETIC,8,1}, /* #DE FPE_INTDIV */
        {4,NAS_EXC_ARITHMETIC,8,2}, /* #OF FPE_INTOVF */
        {5,NAS_EXC_BOUNDS,11,3},    /* #BR SEGV_BNDERR */
        {10,NAS_EXC_PROTECTION,11,0x80}, /* #TS SI_KERNEL */
        {11,NAS_EXC_PROTECTION,11,0x80}, /* #NP */
        {12,NAS_EXC_PROTECTION,11,0x80}, /* #SS */
        {16,NAS_EXC_ARITHMETIC,8,0}, /* #MF unknown FP subtype */
        {19,NAS_EXC_ARITHMETIC,8,0}  /* #XM unknown SIMD subtype */
    };
    for(unsigned i=0;i<sizeof(user_sync)/sizeof(user_sync[0]);i++) {
        x=nas_exception_x86(user_sync[i].vector,0,BASE,0x401000,0x33);
        out=nas_exception_dispatch(&x,&space);
        assert(x.kind==user_sync[i].kind &&
               out.action==NAS_EXC_SIGNAL &&
               out.signo==user_sync[i].signo &&
               out.si_code==user_sync[i].si_code);
        x=nas_exception_x86(user_sync[i].vector,0,BASE,0x401000,0x08);
        assert(!x.user &&
               nas_exception_dispatch(&x,&space).action==NAS_EXC_UNHANDLED);
    }
    static const unsigned kernel_fatal_vectors[]={2,7,8,18,20,21,28,29,30,31};
    for(unsigned i=0;i<sizeof(kernel_fatal_vectors)/sizeof(kernel_fatal_vectors[0]);i++) {
        x=nas_exception_x86(kernel_fatal_vectors[i],0,0,0x401000,0x33);
        assert(nas_exception_dispatch(&x,&space).action==NAS_EXC_UNHANDLED);
    }
    puts("[M88 EXCEPTION PASS] x86 0..31 sync signal policy; ARM/RV cases; kernel/debug fail closed; COW/OOM");
    return 0;
}
