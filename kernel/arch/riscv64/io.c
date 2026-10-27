/* RV64 supervisor device mapping for QEMU virt. Limine HHDM maps RAM,
 * not arbitrary MMIO. Use the live kernel upper-half SATP page tables;
 * user AS roots share these borrowed upper-half entries. */
#include <stdint.h>
#include <stdbool.h>
#include "../../mm/frame.h"
#define PAGE 4096ULL
#define PTE_V (1ULL << 0)
#define PTE_R (1ULL << 1)
#define PTE_W (1ULL << 2)
#define PTE_X (1ULL << 3)
#define PTE_U (1ULL << 4)
#define PTE_A (1ULL << 6)
#define PTE_D (1ULL << 7)
#define PPN_MASK ((1ULL << 44) - 1)
bool nas_rv_map_io_page(uint64_t physical,uint64_t virt) {
    if((physical & (PAGE-1)) || (virt & (PAGE-1)) ||
       !(virt & (1ULL<<63)))return false;
    uint64_t satp;
    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    unsigned mode=(unsigned)(satp>>60);
    if(mode<8 || mode>10)return false; /* Sv39/Sv48/Sv57 */
    unsigned levels=mode-5;
    uint64_t root=(satp&PPN_MASK)<<12;
    if(!root)return false;
    uint64_t pa=root;
    for(unsigned level=levels;level>1;--level) {
        volatile uint64_t *tab=(volatile uint64_t *)(uintptr_t)
            (nas_hhdm_offset()+pa);
        unsigned idx=(unsigned)((virt>>(12+9*(level-1)))&511U);
        uint64_t entry=tab[idx];
        if(!entry) {
            uint64_t child=nas_frame_alloc();
            if(!child)return false;
            __atomic_thread_fence(__ATOMIC_RELEASE);
            tab[idx]=((child>>12)<<10)|PTE_V;
            entry=tab[idx];
        }
        /* No superpage splitting, no reinterpretation of user PTEs. */
        if(!(entry&PTE_V) || (entry&(PTE_R|PTE_W|PTE_X|PTE_U)))
            return false;
        pa=((entry>>10)&PPN_MASK)<<12;
        if(!pa)return false;
    }
    volatile uint64_t *leaf=(volatile uint64_t *)(uintptr_t)
        (nas_hhdm_offset()+pa);
    unsigned idx=(unsigned)((virt>>12)&511U);
    if(leaf[idx])return false;
    /* RISC-V PBMT is optional: standard RV64 virt PMA marks this PA as IO.
     * No executable/user flags; A/D present for supervisor MMIO accesses. */
    leaf[idx]=((physical>>12)<<10)|PTE_V|PTE_R|PTE_W|PTE_A|PTE_D;
    __asm__ volatile("fence rw,rw\nsfence.vma zero,zero"
                     : : : "memory");
    return true;
}
