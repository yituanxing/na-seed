#include <stdint.h>
#include "../errno.h"
#include "../task.h"
#include "../arch/arch.h"
#include "vm.h"
#include "prot.h"
#include "anon_regions.h"
#define TASK (*nas_current_task)
#define heap_break TASK.heap_break

/* M11: eager, privately owned anonymous VM ranges. Existing PT_LOAD and
 * heap/stack mappings stay in their established address domains. Releasing
 * PTE frames without returning the virtual range would eventually exhaust
 * the 0x20000000..0x60000000 window even with abundant free physical RAM.
 * Fixed-count VMA records are per task: fork copies them and exec resets
 * after the new ELF image commits. No COW/lazy fault pager is claimed.
 */
uint64_t nas_vm_anon_mmap(uint64_t hint, uint64_t length,
                         uint64_t prot, uint64_t flags,
                         uint64_t fd, uint64_t offset) {
    (void)hint;
    /* MAP_PRIVATE|MAP_ANONYMOUS; no file/offset/map-fixed emulation. */
    if (!length || length > (64ULL << 20) || length > UINT64_MAX - PAGE)
        return (uint64_t)-EINVAL;
    if ((flags & 0x22) != 0x22 || (flags & ~0x22ULL) ||
        fd != UINT64_MAX || offset)
        return (uint64_t)-ENOSYS;
    if (prot & ~7ULL) return (uint64_t)-EINVAL;
    if (!prot) return (uint64_t)-ENOSYS; /* needs inaccessible-VMA support */
    if (!nas_vm_prot_supported(prot)) return (uint64_t)-EOPNOTSUPP;
    if (!nas_arch_user_nx_supported()) return (uint64_t)-ENOSYS;
    uint64_t pages = (length + PAGE - 1) / PAGE;
    uint64_t bytes = pages * PAGE;
    uint64_t start;
    if (!nas_anon_find_gap(&TASK.anonymous, bytes, &start))
        return (uint64_t)-ENOMEM;
    uint64_t va=start;
    for (; va < start+bytes; va+=PAGE)
        if (!nas_mmu_map_prot_page(va,prot)) break;
    if (va != start+bytes) {
        while (va>start) { va-=PAGE; if (!nas_mmu_unmap_page(va)) nas_arch_halt(); }
        return (uint64_t)-ENOMEM;
    }
    /* A free metadata slot was checked by find_gap; this cannot fail
     * without an internal allocator bug or unexpected PTE aliasing. */
    if (!nas_anon_record(&TASK.anonymous,start,bytes))
        nas_arch_halt();
    return start;
}
uint64_t nas_vm_munmap(uint64_t addr, uint64_t len) {
    if (!len || (addr & (PAGE - 1)) || len > UINT64_MAX - (PAGE - 1))
        return (uint64_t)-EINVAL;
    uint64_t count = (len + PAGE - 1) / PAGE;
    if (addr >= USER_LIMIT || count > (USER_LIMIT - addr) / PAGE)
        return (uint64_t)-EINVAL;
    /* Plan a possibly partial unmap on a metadata copy first. If splitting
     * requires a 65th entry, leave both the VMAs and PTEs unmodified. */
    struct nas_anon_regions updated;
    /* Freestanding 64-entry metadata copy: do not require implicit libc
     * memcpy on any ISA, and preserve the original until all edits succeed. */
    for (unsigned i=0; i<NAS_ANON_REGION_LIMIT; ++i) {
        updated.slots[i].start=TASK.anonymous.slots[i].start;
        updated.slots[i].end=TASK.anonymous.slots[i].end;
    }
    if (!nas_anon_forget(&updated,addr,count*PAGE))
        return (uint64_t)-ENOMEM;
    for (uint64_t i=0; i<count; ++i)
        if (!nas_mmu_unmap_page(addr+i*PAGE)) return (uint64_t)-EFAULT;
    for (unsigned i=0; i<NAS_ANON_REGION_LIMIT; ++i) {
        TASK.anonymous.slots[i].start=updated.slots[i].start;
        TASK.anonymous.slots[i].end=updated.slots[i].end;
    }
    return 0;
}
uint64_t nas_vm_brk(uint64_t want) {
    uint64_t base=heap_break;
    if (!want) return heap_break;
    /* brk is a Linux special: failure returns the unchanged break. */
    if (want < 0x10000 || want >= 0x20000000) return base;
    uint64_t before=(heap_break+PAGE-1)&~(PAGE-1);
    uint64_t after=(want+PAGE-1)&~(PAGE-1);
    if (after>before) {
        uint64_t va=before;
        for (;va<after;va+=PAGE)
            if (!nas_mmu_map_page(va,true)) break;
        if (va!=after) {
            while (va>before) { va-=PAGE; if (!nas_mmu_unmap_page(va)) nas_arch_halt(); }
            return base;
        }
    } else if (after<before) {
        for (uint64_t va=after;va<before;va+=PAGE)
            if (!nas_mmu_unmap_page(va)) return base;
    }
    heap_break=want;
    return want;
}
uint64_t nas_vm_mprotect(uint64_t addr,uint64_t length,uint64_t prot) {
    if (addr & (PAGE-1) || (prot & ~7ULL) ||
        length > UINT64_MAX-(PAGE-1))
        return (uint64_t)-EINVAL;
    if (!length) return 0;
    uint64_t pages=(length+PAGE-1)/PAGE;
    if (addr>=USER_LIMIT || pages>(USER_LIMIT-addr)/PAGE)
        return (uint64_t)-ENOMEM;
    if (!nas_arch_user_nx_supported()) return (uint64_t)-ENOSYS;
    if (!prot) return (uint64_t)-ENOSYS; /* PROT_NONE needs VMA semantics */
    if (!nas_vm_prot_supported(prot)) return (uint64_t)-EOPNOTSUPP;
    return nas_mmu_protect_pages(addr,pages,prot)?0:(uint64_t)-ENOMEM;
}
