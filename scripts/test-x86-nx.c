/* Actual x86 page table implementation, with only privileged CPU registers
 * and the physical-frame allocator simulated. No copied mapping algorithm. */
#include <assert.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/task.h"
#include "../kernel/mm/mm.h"
#include "../kernel/arch/x86_64/mmu.h"
#include "../kernel/arch/mmu.h"
#include "../kernel/arch/user_pt.h"
#define FRAME 4096ULL
#define ROOT 0x1000ULL
#define DATA 0x200000ULL
static alignas(4096) uint8_t physical[32*4096];
static unsigned next_page=2,invalidations;
static bool nx=true;
static struct nas_task task;
static struct nas_mm mm={.users=1,.root_phys=ROOT};
struct nas_task *nas_current_task=&task;
uint64_t nas_hhdm_offset(void){return (uint64_t)(uintptr_t)physical;}
uint64_t nas_frame_alloc(void) {
    assert(next_page<32);
    uint64_t pa=(uint64_t)(next_page++)*FRAME;
    memset(physical+pa,0,FRAME);
    return pa;
}
bool nas_frame_put(uint64_t pa){(void)pa;return true;}
bool nas_arch_nx_enabled(void){return nx;}
void nas_arch_invalidate_page(uint64_t va){(void)va;invalidations++;}
static unsigned access_at(uint64_t va) {
    uint64_t pa=0;unsigned access=0;
    assert(nas_mmu_resolve_user_address(ROOT,va,&pa,&access));
    assert(pa && !(pa&(FRAME-1)));
    return access;
}
int main(void) {
    task.mm=&mm;
    nx=false;
    assert(!nas_mmu_map_page(DATA,true));
    assert(next_page==2);
    nx=true;
    /* The legacy map(true) is used for stack, brk and writable ELF data. */
    assert(nas_mmu_map_page(DATA,true));
    assert(access_at(DATA)==(NAS_UP_READ|NAS_UP_WRITE));
    assert(nas_mmu_map_page(DATA,true));
    assert(access_at(DATA)==(NAS_UP_READ|NAS_UP_WRITE));
    /* The legacy map(false) is currently the ELF text mapping path. */
    assert(nas_mmu_map_page(DATA+FRAME,false)==true);
    assert(access_at(DATA+FRAME)==(NAS_UP_READ|NAS_UP_EXEC));
    /* An overlapping legacy ELF PT_LOAD has not been compatibility-tested
     * in QEMU; preserve the old promotion semantics as an explicit debt. */
    assert(nas_mmu_map_page(DATA+FRAME,true));
    assert(access_at(DATA+FRAME)==
           (NAS_UP_READ|NAS_UP_EXEC|NAS_UP_WRITE));
    /* Inverse ELF order: data RW+NX first, then text RX. The overlapping
     * page must still execute; this preserves existing PT_LOAD semantics. */
    assert(nas_mmu_map_page(DATA+2*FRAME,true));
    assert(access_at(DATA+2*FRAME)==(NAS_UP_READ|NAS_UP_WRITE));
    assert(nas_mmu_map_page(DATA+2*FRAME,false));
    assert(access_at(DATA+2*FRAME)==
           (NAS_UP_READ|NAS_UP_EXEC|NAS_UP_WRITE));
    assert(nas_mmu_map_prot_page(DATA+3*FRAME,1));
    assert(access_at(DATA+3*FRAME)==NAS_UP_READ);
    assert(nas_mmu_map_prot_page(DATA+4*FRAME,5));
    assert(access_at(DATA+4*FRAME)==(NAS_UP_READ|NAS_UP_EXEC));
    assert(invalidations>=4);
    puts("[X86 NX PASS] real PTEs: new stack/brk RW+NX, text RX, both ELF overlap orders retain union");
    return 0;
}
