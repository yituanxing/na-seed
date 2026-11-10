#include "../kernel/arch/x86_64/mmu.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exercise the SAME x86 MMU mapper/unmapper as a QEMU kernel, substituting
 * only the physical-memory allocator and current CR3. This suite never
 * dereferences the PCI device address: only page-table metadata is touched. */
static uint8_t backing[8U*4096U] __attribute__((aligned(4096)));
static uint64_t next_phys=2U*4096U;
static unsigned invalidate_count;
static uint64_t last_invalidated;

uint64_t nas_hhdm_offset(void) {return (uint64_t)(uintptr_t)backing;}
uint64_t nas_arch_current_page_root(void) {return 4096U;}
uint64_t nas_frame_alloc(void) {
    if(next_phys>=sizeof(backing))return 0;
    uint64_t pa=next_phys;
    next_phys+=4096U;
    memset(backing+pa,0,4096U);
    return pa;
}
void nas_arch_invalidate_page(uint64_t va) {
    ++invalidate_count;
    last_invalidated=va;
}

static void check(bool good,const char *why) {
    if(!good) {
        fprintf(stderr,"[H7 X86 MMIO MMU HOST FAIL] %s\n",why);
        exit(1);
    }
}

static uint64_t *leaf(uint64_t va) {
    uint64_t pa=4096U;
    for(unsigned level=3;level>0;--level) {
        uint64_t *table=(uint64_t *)(void *)(backing+pa);
        uint64_t e=table[(va>>(12U+9U*level))&511U];
        if(!(e&1U))return NULL;
        pa=e&UINT64_C(0x000ffffffffff000);
    }
    return &((uint64_t *)(void *)(backing+pa))[(va>>12)&511U];
}

int main(void) {
    const uint64_t va=UINT64_C(0xfffffe8001100000);
    const uint64_t phys=UINT64_C(0x000000c000004000);
    check(nas_arch_x86_map_device_page(phys,va) &&
          nas_arch_x86_map_device_page(phys+4096U,va+4096U),
          "two real supervisor UC/RW/NX leaves installed");
    uint64_t *first=leaf(va),*second=leaf(va+4096U);
    check(first && second && *first && *second,
          "both physical BAR pages have page-table leaves");
    uint64_t saved_first=*first,saved_second=*second;
    unsigned inv_before=invalidate_count;
    check(!nas_arch_x86_unmap_device_pages(phys+4096U,va,2U) &&
          *first==saved_first && *second==saved_second &&
          invalidate_count==inv_before,
          "wrong physical BAR range rejected without partial mutation");
    check(!nas_arch_x86_unmap_device_pages(phys,va+1U,2U) &&
          !nas_arch_x86_unmap_device_pages(phys,va,0U),
          "unaligned/zero unmap input rejected");
    *second|=4U; /* user bit: never permit device unmap to remove user VA */
    check(!nas_arch_x86_unmap_device_pages(phys,va,2U) &&
          *first==saved_first && *second==(saved_second|4U),
          "second-leaf user-bit corruption preserves the first leaf");
    *second=saved_second;
    *second|=UINT64_C(1)<<7; /* PAT memory type must remain exactly UC */
    check(!nas_arch_x86_unmap_device_pages(phys,va,2U) &&
          *first==saved_first,
          "PAT/alias memory-type corruption cannot release MMIO resource");
    *second=saved_second;
    *second&=~UINT64_C(0x8000000000000000);
    check(!nas_arch_x86_unmap_device_pages(phys,va,2U) &&
          *first==saved_first,
          "executable alias cannot be unmapped by UC/NX resource helper");
    *second=saved_second;
    check(nas_arch_x86_unmap_device_pages(phys,va,2U) &&
          !*first && !*second &&
          invalidate_count==inv_before+2U &&
          last_invalidated==va+4096U,
          "exact two-page unmap flushes both TLB entries");
    check(!nas_arch_x86_unmap_device_pages(phys,va,2U),
          "double unmap cannot claim a second resource release");
    check(nas_arch_x86_map_readonly_device_page(phys,va) &&
          nas_arch_x86_unmap_device_pages(phys,va,1U) && !*first,
          "read-only UC/NX ECAM-style leaf is also safely reversible");
    check(nas_arch_x86_map_device_page(phys,va) &&
          !nas_arch_x86_unmap_device_pages(phys,va,2U) && *first &&
          nas_arch_x86_unmap_device_pages(phys,va,1U),
          "partially installed two-page BAR cannot silently unmap/release");
    check(!nas_arch_x86_unmap_device_pages(phys,
                         UINT64_C(0xfffffffffffff000),2U),
          "wraparound supervisor MMIO alias rejected");
    puts("[H7 X86 MMIO MMU HOST PASS] exact PTE/UC/NX match, transactional failure, TLB invalidation, RO and partial maps");
    return 0;
}
