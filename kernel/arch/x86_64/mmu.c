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
/* Generic supervisor-only, uncached device PTE. The same MMU primitive
 * handles LAPIC and PCI BARs; never create a per-device page walker. */
bool nas_arch_x86_map_device_page(uint64_t phys,uint64_t virt) {
    if (!phys || (phys & 4095U) || (virt & 4095U) ||
        virt < UINT64_C(0xffff800000000000)) return false;
    uint64_t root=nas_arch_current_page_root() & PHYS_MASK;
    if (!root) return false;
    for (unsigned level=3;level>0;--level) {
        uint64_t *table=(uint64_t *)(uintptr_t)(nas_hhdm_offset()+root);
        unsigned idx=(unsigned)((virt>>(12+9*level))&511U);
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
    unsigned idx=(unsigned)((virt>>12)&511U);
    if (pt[idx]) return false;
    /* Write-back caching on LAPIC MMIO is forbidden. PCD+PWT, NX, supervisor. */
    pt[idx]=phys|P_PRESENT|P_RW|0x18ULL|(1ULL<<63);
    nas_arch_invalidate_page(virt);
    return true;
}
bool nas_arch_x86_map_lapic_page(uint64_t phys) {
    return nas_arch_x86_map_device_page(phys,NAS_X86_LAPIC_VIRT);
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
/* Every execve owns a fresh page-table root; the supervisor upper half is
 * inherited without transferring ownership of its boot mappings. The old
 * root is reclaimed only after CR3 switches. Fork/COW is owned by MM. */
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
        if (!nas_frame_put(pa)) return false;
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
        if (!nas_frame_put(child)) return false;
    }
    return nas_frame_put(pa);
}
/* Installing CR3 changes CPU execution state, not MM ownership.
 * In particular, a shared CLONE_VM MM must never be silently rewritten. */
void nas_mmu_switch_root(uint64_t pa) {
    nas_arch_switch_page_root(pa);
}


static uint64_t *user_leaf_at(uint64_t root_phys,uint64_t va,
                              bool allocate_tables) {
    if (!root_phys || (root_phys&(PAGE-1)) ||
        va < PAGE || va >= USER_LIMIT || (va & (PAGE - 1))) return 0;
    uint64_t table = root_phys;
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
static uint64_t *user_leaf(uint64_t va,bool allocate_tables) {
    return user_leaf_at(TASK.mm->root_phys,va,allocate_tables);
}
bool nas_mmu_prepare_leaf_at(uint64_t root_phys,uint64_t va) {
    uint64_t *pte=user_leaf_at(root_phys,va,true);
    return pte && (!*pte || ((*pte&(P_PRESENT|P_USER))==(P_PRESENT|P_USER)));
}
bool nas_mmu_prepare_leaf(uint64_t va) {
    return nas_mmu_prepare_leaf_at(TASK.mm->root_phys,va);
}
bool nas_mmu_exchange_leaf_at(uint64_t root_phys,uint64_t va,
                              uint64_t new_pa,unsigned prot,
                              uint64_t *old_pa) {
    if(!old_pa || (new_pa && ((new_pa&(PAGE-1)) ||
       !nas_vm_prot_supported(prot) || !nas_arch_nx_enabled())) ||
       (!new_pa && prot))return false;
    uint64_t *pte=user_leaf_at(root_phys,va,false);
    if(!pte || (*pte && ((*pte&(P_PRESENT|P_USER))!=(P_PRESENT|P_USER))))
        return false;
    uint64_t previous=*pte;
    const uint64_t nx=1ULL<<63;
    *pte=new_pa ? new_pa|P_PRESENT|P_USER|
        ((prot&NAS_VM_WRITE)?P_RW:0)|((prot&NAS_VM_EXEC)?0:nx) : 0;
    nas_arch_invalidate_page(va);
    *old_pa=(previous&P_PRESENT)?(previous&PHYS_MASK):0;
    return true;
}
bool nas_mmu_exchange_leaf(uint64_t va,uint64_t new_pa,unsigned prot,
                           uint64_t *old_pa) {
    return nas_mmu_exchange_leaf_at(TASK.mm->root_phys,va,new_pa,prot,old_pa);
}
bool nas_mmu_map_page(uint64_t va, bool writable) {
    /* Legacy ELF/stack/brk entry point. New writable user pages start RW+NX;
     * new nonwritable ELF pages start RX. The historical ELF loader maps
     * PT_LOAD segments in order and a boundary page can be mapped twice in
     * EITHER order. Preserve the union of requested permissions in that
     * exceptional overlap (RWX), pending a typed whole-page ELF preflight. */
    if(!nas_arch_nx_enabled())return false;
    uint64_t *entry = user_leaf(va, true);
    if (!entry) return false;
    if (*entry & P_PRESENT) {
        if (!(*entry & P_USER)) return false;
        if (writable) *entry |= P_RW;
        else *entry &= ~(1ULL<<63); /* prior RW -> overlapping RX */
        nas_arch_invalidate_page(va);
        return true;
    }
    uint64_t pa = nas_frame_alloc();
    if (!pa) return false;
    *entry = pa | P_PRESENT | P_USER | (writable ? (P_RW|(1ULL<<63)) : 0);
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
    return nas_frame_put(pa);
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
