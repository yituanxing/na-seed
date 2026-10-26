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
    /* COW is NOT silently treated as a fault to be made writable. */
    x.addr=BASE;present_access=NAS_UP_READ;
    out=nas_exception_dispatch(&x,&space);
    assert(out.action==NAS_EXC_UNHANDLED &&
           nas_mm_handle_fault(&space,BASE,NAS_UP_WRITE,false)==NAS_MM_FAULT_NEEDS_COW);
    present_access=NAS_UP_READ|NAS_UP_WRITE;
    x.addr=BASE+4*PAGE;x.access=NAS_EXC_READ;
    assert(nas_mm_handle_fault(&space,x.addr,NAS_UP_READ,false)==NAS_MM_FAULT_NEEDS_BACKING);
    out=nas_exception_dispatch(&x,&space);
    assert(out.action==NAS_EXC_UNHANDLED);
    unmap_mock();
    x.addr=BASE+PAGE;
    fail_alloc=true;
    assert(nas_mm_handle_fault(&space,x.addr,NAS_UP_WRITE,false)==NAS_MM_FAULT_OOM);
    fail_alloc=false;fail_prepare=true;
    unsigned before=freed;
    assert(nas_mm_handle_fault(&space,x.addr,NAS_UP_WRITE,false)==NAS_MM_FAULT_OOM);
    assert(freed==before+1 && !present_pa);
    fail_prepare=false;
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
    struct nas_exception arm=nas_exception_arm64((0x24ULL<<26)|0x44,0,
                                                  BASE+PAGE,0x7000);
    assert(arm.kind==NAS_EXC_MEMORY && arm.user &&
           arm.access==NAS_EXC_WRITE);
    arm=nas_exception_arm64((0x20ULL<<26)|0x0d,0,BASE,0x7000);
    assert(arm.kind==NAS_EXC_MEMORY && arm.access==NAS_EXC_EXEC && arm.present);
    arm=nas_exception_arm64((0x15ULL<<26),0,0,0);
    assert(arm.kind==NAS_EXC_SYSCALL);
    x=nas_exception_x86(14,0x14,BASE,0,0x33);
    assert(x.access==NAS_EXC_EXEC && !x.present);
    assert(nas_exception_x86(1,0,0,0,0x33).kind==NAS_EXC_SINGLE_STEP);
    assert(nas_exception_x86(3,0,0,0,0x33).kind==NAS_EXC_BREAKPOINT);
    puts("[M88 EXCEPTION PASS] 3 ISA cause decoders; VMA policy; anonymous repair; COW/backing fail-closed; OOM rollback");
    return 0;
}
