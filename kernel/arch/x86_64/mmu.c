#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../arch.h"
#include "../../task.h"
#include "../../mm/frame.h"
#include "mmu.h"
#include "../../mm/prot.h"
#include "../user_pt.h"
#include "../mmu.h"
#define TASK (*nas_current_task)
#define P_PRESENT 1ULL
#define P_RW 2ULL
#define P_USER 4ULL
#define PHYS_MASK 0x000ffffffffff000ULL
extern void nas_print(const char *);
/* Local APIC physical MMIO is not in Limine's RAM-only HHDM.
 * Use a dedicated kernel high-VA leaf shared by every future user CR3.
 * All intermediate pages come from the regular zeroing frame allocator.
 * Never overwrite a firmware/kernel mapping or interpret a huge page as
 * a next-level table. 0xffffffffe1000000 is uses otherwise-unclaimed canonical PML4 slot 509, outside both the
 * kernel image slot 511 and ordinary Limine HHDM slots.
 */
#define NAS_X86_LAPIC_VIRT UINT64_C(0xfffffe8000000000)
bool nas_arch_x86_map_lapic_page(uint64_t phys) {
    if (!phys || (phys & 4095U)) return false;
    uint64_t root=nas_arch_current_page_root() & PHYS_MASK;
    if (!root) return false;
    for (unsigned level=3;level>0;--level) {
        uint64_t *table=(uint64_t *)(uintptr_t)(nas_hhdm_offset()+root);
        unsigned idx=(unsigned)((NAS_X86_LAPIC_VIRT>>(12+9*level))&511U);
        uint64_t entry=table[idx];
        if (!(entry&P_PRESENT)) {
            root=nas_frame_alloc();
            if (!root) return false;
            table[idx]=root|P_PRESENT|P_RW;
        } else {
            if ((entry & P_USER) || (entry & (1ULL<<7))) return false;
            root=entry&PHYS_MASK;
            if (!root) return false;
        }
    }
    uint64_t *pt=(uint64_t *)(uintptr_t)(nas_hhdm_offset()+root);
    unsigned idx=(unsigned)((NAS_X86_LAPIC_VIRT>>12)&511U);
    if (pt[idx]) return false;
    /* Write-back caching on LAPIC MMIO is forbidden. PCD+PWT, NX, supervisor. */
    pt[idx]=phys|P_PRESENT|P_RW|0x18ULL|(1ULL<<63);
    nas_arch_invalidate_page(NAS_X86_LAPIC_VIRT);
    return true;
}
/* User pages: read and amend existing Limine CR3 paging hierarchy. */
static uint64_t *phys_table(uint64_t physical) {
    return (uint64_t *)(uintptr_t)(nas_hhdm_offset() + physical);
}

/* Read-only walk of the *selected task's* user PTEs; no CR3 switch.
 * Always bound the root to lower 256 PML4 entries; refuse huge user pages. */
static bool nas_x86_walk_level(uint64_t pa,unsigned level,uint64_t base,
                               unsigned entries,nas_mmu_user_visit cb,void *arg){
    const uint64_t *t=phys_table(pa);
    for(unsigned i=0;i<entries;i++) {
        uint64_t e=t[i];
        if(!(e&P_PRESENT))continue;
        if(!(e&P_USER) || (e&(1ULL<<7)))return false;
        uint64_t va=base+((uint64_t)i<<(12U+9U*level));
        if(level==0) {
            unsigned access=NAS_UP_READ | ((e&P_RW)?NAS_UP_WRITE:0) |
                            ((e&(1ULL<<63))?0:NAS_UP_EXEC);
            if(!cb(va,access,arg))return false;
        } else if(!nas_x86_walk_level(e&PHYS_MASK,level-1,va,512,cb,arg))
            return false;
    }
    return true;
}
bool nas_mmu_resolve_user_address(uint64_t selected_root,uint64_t user_va,
                                  uint64_t *physical,unsigned *access) {
    if(!selected_root || (selected_root&(PAGE-1)) ||
       !physical || user_va>=USER_LIMIT)return false;
    uint64_t pa=selected_root;
    for(unsigned level=3;level>0;--level) {
        const uint64_t *tab=phys_table(pa);
        unsigned slot=(unsigned)((user_va>>(12U+9U*level))&511ULL);
        uint64_t entry=tab[slot];
        if((entry&(P_PRESENT|P_USER))!=(P_PRESENT|P_USER) ||
           (entry&(1ULL<<7)))return false;
        pa=entry&PHYS_MASK;
        if(!pa)return false;
    }
    uint64_t leaf=phys_table(pa)[(user_va>>12)&511ULL];
    if((leaf&(P_PRESENT|P_USER))!=(P_PRESENT|P_USER))return false;
    *physical=(leaf&PHYS_MASK)|(user_va&(PAGE-1));
    if(access)*access=NAS_UP_READ|((leaf&P_RW)?NAS_UP_WRITE:0)|
                      ((leaf&(1ULL<<63))?0:NAS_UP_EXEC);
    return true;
}
bool nas_mmu_walk_user_mapping(uint64_t target_root,
                                nas_mmu_user_visit cb,void *arg) {
    if(!target_root || (target_root&(PAGE-1)) || !cb)return false;
    return nas_x86_walk_level(target_root,3,0,256,cb,arg);
}
bool nas_mmu_bootstrap(void) {
    uint64_t cr3 = nas_arch_current_page_root();
    if (nas_arch_la57_enabled()) return false; /* LA57 not part of M1. */
    /* Separate userspace CR3: copy supervisor upper half, zero the lower half. */
    uint64_t boot_root = cr3 & PHYS_MASK;
    if (!boot_root) return false;
    TASK.mm->root_phys = nas_frame_alloc();
    if (!TASK.mm->root_phys) return false;
    uint64_t *old = phys_table(boot_root);
    uint64_t *fresh = phys_table(TASK.mm->root_phys);
    for (unsigned i = 256; i < 512; i++)
        fresh[i] = old[i] & ~P_USER;
    nas_arch_switch_page_root(TASK.mm->root_phys);
    return true;
}
/* Every execve owns a fresh user page-table root; the supervisor upper
 * half is inherited without allocating or freeing its boot mappings.
 * The retired root is reclaimed ONLY after CR3 switches successfully.
 * User pages are private: NaSeed has no fork/COW or shared mappings yet.
 */
uint64_t nas_mmu_alloc_root(void) {
    uint64_t pa = nas_frame_alloc();
    if (!pa) return 0;
    uint64_t *fresh = phys_table(pa), *current = phys_table(TASK.mm->root_phys);
    for (unsigned i = 256; i < 512; i++)
        fresh[i] = current[i] & ~P_USER;
    return pa;
}
static bool free_user_tree(uint64_t table_pa, unsigned depth) {
    uint64_t *table = phys_table(table_pa);
    for (unsigned i = 0; i < 512; i++) {
        uint64_t e = table[i];
        if (!(e & P_PRESENT)) continue;
        if (!(e & P_USER) || (e & (1ULL << 7))) return false;
        uint64_t pa = e & PHYS_MASK;
        if (depth && !free_user_tree(pa, depth - 1)) return false;
        table[i] = 0;
        if (!nas_frame_free(pa)) return false;
    }
    return true;
}
bool nas_mmu_reclaim_root(uint64_t pa) {
    uint64_t *root = phys_table(pa);
    for (unsigned i = 0; i < 256; i++) {
        uint64_t e = root[i];
        if (!(e & P_PRESENT)) continue;
        if (!(e & P_USER) || (e & (1ULL << 7))) return false;
        uint64_t child = e & PHYS_MASK;
        if (!free_user_tree(child, 2)) return false;
        root[i] = 0;
        if (!nas_frame_free(child)) return false;
    }
    return nas_frame_free(pa);
}
void nas_mmu_switch_root(uint64_t pa) {
    TASK.mm->root_phys = pa;
    nas_arch_switch_page_root(pa);
}


/* Copy every PRESENT user page into independent physical frames.
 * This is eager fork, not COW: parent/child writes must not alias.
 * Lower-half mappings are 4K; huge user mappings are not implemented.
 * On partial OOM, nas_mmu_reclaim_root() frees exactly the created tree.
 */
static bool clone_user_level(uint64_t source_pa, uint64_t dest_pa,
                             unsigned depth, unsigned entries) {
    uint64_t *source = phys_table(source_pa);
    uint64_t *dest = phys_table(dest_pa);
    for (unsigned i = 0; i < entries; ++i) {
        uint64_t e = source[i];
        if (!(e & P_PRESENT)) continue;
        if (!(e & P_USER) || (e & (1ULL << 7))) return false;
        uint64_t child_pa = nas_frame_alloc();
        if (!child_pa) return false;
        dest[i] = child_pa | (e & ~PHYS_MASK);
        if (depth) {
            if (!clone_user_level(e & PHYS_MASK, child_pa, depth - 1, 512))
                return false;
        } else {
            uint64_t *from = phys_table(e & PHYS_MASK);
            uint64_t *to = phys_table(child_pa);
            for (unsigned n = 0; n < PAGE / 8; n++) to[n] = from[n];
        }
    }
    return true;
}
uint64_t nas_mmu_fork_root(void) {
    uint64_t destination = nas_mmu_alloc_root();
    if (!destination) return 0;
    if (clone_user_level(TASK.mm->root_phys, destination, 3, 256)) return destination;
    if (!nas_mmu_reclaim_root(destination))
        { nas_print("[FAIL] M1 fork private page rollback corrupted\n"); nas_arch_halt(); }
    return 0;
}
static uint64_t *user_leaf(uint64_t va, bool allocate_tables) {
    if (va < PAGE || va >= USER_LIMIT || (va & (PAGE - 1))) return 0;
    uint64_t table = TASK.mm->root_phys;
    for (int level = 3; level >= 1; --level) {
        unsigned index = (va >> (12 + (unsigned)level * 9)) & 511;
        uint64_t *entries = phys_table(table);
        uint64_t e = entries[index];
        if (!(e & P_PRESENT)) {
            if (!allocate_tables) return 0;
            uint64_t pa = nas_frame_alloc();
            if (!pa) return 0;
            entries[index] = pa | P_PRESENT | P_RW | P_USER;
            e = entries[index];
        }
        if (!(e & P_USER) || (e & (1ULL << 7))) return 0;
        table = e & PHYS_MASK;
    }
    return &phys_table(table)[(va >> 12) & 511];
}
bool nas_mmu_map_page(uint64_t va, bool writable) {
    uint64_t *entry = user_leaf(va, true);
    if (!entry) return false;
    if (*entry & P_PRESENT) {
        if (!(*entry & P_USER)) return false;
        if (writable) *entry |= P_RW;
        nas_arch_invalidate_page(va);
        return true;
    }
    uint64_t pa = nas_frame_alloc();
    if (!pa) return false;
    *entry = pa | P_PRESENT | P_USER | (writable ? P_RW : 0);
    nas_arch_invalidate_page(va);
    return true;
}
bool nas_mmu_map_prot_page(uint64_t va, uint64_t prot) {
    /* First-set PTE permissions: do not transiently create executable
     * anonymous RW data, and do not pretend W+X / PROT_NONE is supported. */
    if (!nas_vm_prot_supported(prot) || !nas_arch_nx_enabled()) return false;
    uint64_t *entry = user_leaf(va, true);
    if (!entry || *entry) return false;
    uint64_t pa = nas_frame_alloc();
    if (!pa) return false;
    const uint64_t nx = 1ULL << 63;
    *entry = pa | P_PRESENT | P_USER |
             ((prot & NAS_VM_WRITE) ? P_RW : 0) |
             ((prot & NAS_VM_EXEC) ? 0 : nx);
    nas_arch_invalidate_page(va);
    return true;
}
uint8_t *nas_mmu_user_byte(uint64_t va) {
    uint64_t *entry = user_leaf(va & ~(PAGE - 1), false);
    /* A syscall may only dereference present user pages, never supervisor
     * entries or an address outside the user half of this address space.
     * HHDM physical aliases bypass the CPU's normal U/S protection.
     */
    if (!entry || (*entry & (P_PRESENT | P_USER)) != (P_PRESENT | P_USER))
        return 0;
    return (uint8_t *)(uintptr_t)(nas_hhdm_offset() +
        (*entry & PHYS_MASK) + (va & (PAGE - 1)));
}

bool nas_mmu_unmap_page(uint64_t va) {
    uint64_t *pte = user_leaf(va, false);
    if (!pte || !(*pte & P_PRESENT) || !(*pte & P_USER)) return true;
    uint64_t pa = *pte & PHYS_MASK;
    *pte = 0;
    nas_arch_invalidate_page(va);
    return nas_frame_free(pa);
}


bool nas_mmu_user_writable(uint64_t addr) {
    uint64_t *pte = user_leaf(addr & ~(PAGE - 1), false);
    return pte && (*pte & (P_PRESENT | P_USER | P_RW)) == (P_PRESENT | P_USER | P_RW);
}
/* First pass validates every page, preserving the original all-or-nothing PTE edit. */
bool nas_mmu_protect_pages(uint64_t addr, uint64_t pages, uint64_t prot) {
    const uint64_t nx = 1ULL << 63;
    for (uint64_t i = 0; i < pages; i++) {
        uint64_t *pte = user_leaf(addr + i * PAGE, false);
        if (!pte || !(*pte & P_PRESENT) || !(*pte & P_USER))
            return false;
    }
    for (uint64_t i = 0; i < pages; i++) {
        uint64_t va = addr + i * PAGE;
        uint64_t *pte = user_leaf(va, false);
        uint64_t e = *pte & ~(P_RW | nx);
        if (prot & 2) e |= P_RW;
        if (!(prot & 4)) e |= nx;
        *pte = e;
        nas_arch_invalidate_page(va);
    }
    return true;
}
