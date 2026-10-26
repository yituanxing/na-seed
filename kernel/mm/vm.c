#include <stdint.h>
#include "../errno.h"
#include "../task.h"
#include "../arch/arch.h"
#include "vm.h"
#include "prot.h"
#include "anon_regions.h"
#include "../lib/string.h"
#include "../fs/io.h"
#include "../arch/mmu.h"
#include "mmap_abi.h"
bool nas_vm_walk_task_vmas(const struct nas_task *task,
                            nas_vma_visit callback,void *ctx) {
    return task && task->state!=NAS_TASK_FREE && task->mm &&
           task->mm->users && nas_vma_walk(&task->mm->mmap_vmas,callback,ctx);
}
bool nas_vm_walk_task_pages(const struct nas_task *task,
                             nas_mmu_user_visit callback,void *ctx) {
    if(!task || !callback || task->state==NAS_TASK_FREE)return false;
    return nas_mm_walk_pages(task->mm,callback,ctx);
}
#define TASK (*nas_current_task)
#define heap_break TASK.mm->heap_break
/* Ordinary anonymous allocation stays in the top-down window. Fixed
 * mappings participate in the same per-task VM ownership ledger, including
 * the lower program/heap address domain used by original GCC + musl. */

/* M85-A: typed, independently MM-owned private mmap VMAs. Existing PT_LOAD and
 * heap/stack mappings stay in their established address domains. Releasing
 * PTE frames without returning the virtual range would eventually exhaust
 * the 0x20000000..0x60000000 window even with abundant free physical RAM.
 * Bounded VMA records are per MM: fork copies them and exec resets
 * after the new ELF image commits. No COW/lazy fault pager is claimed.
 */
uint64_t nas_vm_anon_mmap(uint64_t hint, uint64_t length,
                         uint64_t prot, uint64_t flags,
                         uint64_t fd, uint64_t offset) {
    /* MAP_PRIVATE|MAP_ANONYMOUS. Linux ignores fd under MAP_ANONYMOUS:
     * an x86-64 libc may pass int -1 zero-extended to 0x00000000ffffffff.
     * Non-anonymous mappings and nonzero offsets remain explicitly refused. */
    if (!length || length > (64ULL << 20) || length > UINT64_MAX - PAGE)
        return (uint64_t)-EINVAL;
    bool fixed=nas_linux_anon_fixed_supported(flags,offset);
    if (!fixed && !nas_linux_anon_map_supported(flags,fd,offset))
        return (uint64_t)-ENOSYS;
    if (prot & ~7ULL) return (uint64_t)-EINVAL;
    if (prot && !nas_vm_prot_supported(prot)) return (uint64_t)-EOPNOTSUPP;
    if (!nas_arch_user_nx_supported()) return (uint64_t)-ENOSYS;
    uint64_t pages = (length + PAGE - 1) / PAGE;
    uint64_t bytes = pages * PAGE;
    uint64_t start;
    if (fixed) {
        if ((hint & (PAGE-1)) || hint<NAS_ANON_FIXED_FLOOR ||
            hint>=NAS_ANON_CEILING || bytes>NAS_ANON_CEILING-hint)
            return (uint64_t)-EINVAL;
        /* Linux MAP_FIXED REPLACES a user mapping. It is not limited to
         * one previously created mmap VMA: libc may protect the brk/ELF
         * address domain, which is mapped by our existing shared loader.
         * Stage ledger changes first, and keep all kernel/stack VAs out. */
        struct nas_anon_regions planned=TASK.mm->mmap_vmas;
        if (!nas_vma_replace(&planned,hint,bytes,(unsigned)prot,
                             NAS_VMA_ANON_PRIVATE,0))
            return (uint64_t)-ENOMEM;
        start=hint;
        for (uint64_t va=start;va<start+bytes;va+=PAGE) {
            if (nas_mmu_user_byte(va) && !nas_mmu_unmap_page(va))
                return (uint64_t)-EFAULT;
            if (prot && !nas_mmu_map_prot_page(va,prot))
                return (uint64_t)-ENOMEM;
        }
        TASK.mm->mmap_vmas=planned;
        return start;
    }
    if (!nas_anon_find_gap(&TASK.mm->mmap_vmas,bytes,&start))
        return (uint64_t)-ENOMEM;
    /* Stage logical policy BEFORE eager frame allocation. A full registry
     * cannot turn an OOM into a kernel halt after mapping physical memory. */
    struct nas_anon_regions planned=TASK.mm->mmap_vmas;
    if (!nas_vma_record(&planned,start,bytes,(unsigned)prot,
                        NAS_VMA_ANON_PRIVATE,0))return (uint64_t)-ENOMEM;
    /* PROT_NONE owns a VMA but has no resident PTE. */
    uint64_t va=start;
    if (prot) {
        for (;va<start+bytes;va+=PAGE)
            if (!nas_mmu_map_prot_page(va,prot)) break;
        if (va != start+bytes) {
            while (va>start) { va-=PAGE; if (!nas_mmu_unmap_page(va)) nas_arch_halt(); }
            return (uint64_t)-ENOMEM;
        }
    }
    TASK.mm->mmap_vmas=planned;
    return start;
}
/* CPIO/tmpfs MAP_PRIVATE eagerly copies bytes into owned frames.
 * Fixed remaps may replace an existing ELF/heap user address range;
 * anonymous and file mappings use the same metadata and page ownership. */
uint64_t nas_vm_file_mmap(uint64_t hint, uint64_t length, uint64_t prot,
                          uint64_t flags, uint64_t fd, uint64_t offset) {
    const uint64_t MAP_PRIVATE=0x02ULL, MAP_FIXED=0x10ULL;
    if (!length || length > (64ULL<<20) ||
        length > UINT64_MAX-(PAGE-1) ||
        (offset & (PAGE-1)) || offset > UINT64_MAX-length)
        return (uint64_t)-EINVAL;
    /* MAP_SHARED is NOT a private copy. Never silently write back/alias
     * anonymous memory or fabricate success for unsupported semantics. */
    if ((flags & ~(MAP_PRIVATE|MAP_FIXED)) || !(flags & MAP_PRIVATE))
        return (uint64_t)-ENOSYS;
    if (!prot || (prot & ~7ULL)) return (uint64_t)-ENOSYS;
    if (!nas_vm_prot_supported(prot) || !nas_arch_user_nx_supported())
        return (uint64_t)-EOPNOTSUPP;
    int64_t valid=nas_fs_mmap_read(fd,offset,0,0);
    if (valid<0) return (uint64_t)valid;
    uint64_t count=(length+PAGE-1)/PAGE, bytes=count*PAGE;
    uint64_t start;
    struct nas_anon_regions planned=TASK.mm->mmap_vmas;
    if (flags & MAP_FIXED) {
        if ((hint & (PAGE-1)) || hint<NAS_ANON_FIXED_FLOOR ||
            hint>=NAS_ANON_CEILING || bytes>NAS_ANON_CEILING-hint)
            return (uint64_t)-EINVAL;
        if (!nas_vma_replace(&planned,hint,bytes,(unsigned)prot,
                             NAS_VMA_FILE_PRIVATE_SNAPSHOT,offset))
            return (uint64_t)-ENOMEM;
        start=hint;
    } else {
        start=nas_vm_anon_mmap(0,length,prot,0x22ULL,UINT64_MAX,0);
        if ((int64_t)start<0) return start;
    }
    /* Seed every page from the actual FD, including zero-fill beyond EOF.
     * Copy through the common HHDM translator; do NOT write via a user
     * read-only mapping, or depend on an architecture-specific CPIO path. */
    uint8_t page[PAGE];
    for (uint64_t n=0;n<count;n++) {
        uint64_t va=start+n*PAGE;
        if (flags & MAP_FIXED) {
            if (nas_mmu_user_byte(va) && !nas_mmu_unmap_page(va))
                return (uint64_t)-EFAULT;
            if (!nas_mmu_map_prot_page(va,prot))
                return (uint64_t)-ENOMEM;
        }
        uint8_t *mapped=nas_mmu_user_byte(va);
        if (!mapped) return (uint64_t)-EFAULT;
        memset(page,0,sizeof(page));
        uint64_t remaining=length-n*PAGE;
        if (remaining>PAGE) remaining=PAGE;
        int64_t rd=nas_fs_mmap_read(fd,offset+n*PAGE,page,remaining);
        if (rd<0) return (uint64_t)rd;
        memcpy(mapped,page,PAGE);
        if ((flags & MAP_FIXED) &&
            !nas_mmu_protect_pages(va,1,prot))
            return (uint64_t)-ENOMEM;
    }
    if (flags & MAP_FIXED) TASK.mm->mmap_vmas=planned;
    else {
        /* The anonymous eager allocation is now a private copy of this file.
         * Metadata keeps logical mapping type/offset after FD close, but no
         * file lifetime is held because the snapshot is already resident. */
        planned=TASK.mm->mmap_vmas; /* reuse the one staging buffer */
        if(!nas_vma_replace(&planned,start,bytes,(unsigned)prot,
                            NAS_VMA_FILE_PRIVATE_SNAPSHOT,offset)) {
            (void)nas_vm_munmap(start,length);
            return (uint64_t)-ENOMEM;
        }
        TASK.mm->mmap_vmas=planned;
    }
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
    /* Stage changes to VMA metadata before committing any page changes. */
    memcpy(&updated, &TASK.mm->mmap_vmas, sizeof(updated));
    if (!nas_anon_forget(&updated,addr,count*PAGE))
        return (uint64_t)-ENOMEM;
    for (uint64_t i=0; i<count; ++i) {
        uint64_t va=addr+i*PAGE;
        /* PROT_NONE reservations have intentionally absent PTEs. */
        if (nas_mmu_user_byte(va) && !nas_mmu_unmap_page(va))
            return (uint64_t)-EFAULT;
    }
    memcpy(&TASK.mm->mmap_vmas, &updated, sizeof(updated));
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
        /* A prior MAP_FIXED reservation must not be silently destroyed by
         * a later brk expansion: heap pages and mmap pages are disjoint. */
        if (nas_anon_intersects(&TASK.mm->mmap_vmas,before,after-before))
            return base;
        /* Heap VMA creation must be representable before physical mapping. */
        struct nas_anon_regions planned=TASK.mm->mmap_vmas;
        if(!nas_vma_record(&planned,before,after-before,3,NAS_VMA_HEAP,0))
            return base;
        uint64_t va=before;
        for (;va<after;va+=PAGE)
            if (!nas_mmu_map_page(va,true)) break;
        if (va!=after) {
            while (va>before) { va-=PAGE; if (!nas_mmu_unmap_page(va)) nas_arch_halt(); }
            return base;
        }
        TASK.mm->mmap_vmas=planned;
    } else if (after<before) {
        struct nas_anon_regions planned=TASK.mm->mmap_vmas;
        if (!nas_anon_forget(&planned,after,before-after)) return base;
        for (uint64_t va=after;va<before;va+=PAGE)
            if (!nas_mmu_unmap_page(va)) return base;
        TASK.mm->mmap_vmas=planned;
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
    if (!prot) return (uint64_t)-ENOSYS; /* Moving populated pages to PROT_NONE needs backing preservation. */
    if (!nas_vm_prot_supported(prot)) return (uint64_t)-EOPNOTSUPP;
    /* musl pthread_create reserves MAP_PRIVATE|MAP_ANONYMOUS PROT_NONE,
     * then mprotects the stack/TLS interior RW. The virtual reservation is
     * accounted for in the shared VMA ledger, but PROT_NONE has no PTE.
     * Materialize these independently owned, zero-filled pages on demand;
     * NEVER invent user pages in ELF, heap or unreserved address ranges.
     * Return a real ENOMEM when the physical allocator is exhausted. */
    if (nas_anon_covered(&TASK.mm->mmap_vmas,addr,pages*PAGE)) {
        struct nas_anon_regions planned=TASK.mm->mmap_vmas;
        if(!nas_vma_protect(&planned,addr,pages*PAGE,(unsigned)prot))
            return (uint64_t)-ENOMEM;
        /* First validate all pre-existing mapped pages before mutating. */
        for (uint64_t i=0;i<pages;i++) {
            uint64_t va=addr+i*PAGE;
            if (nas_mmu_user_byte(va) &&
                !nas_mmu_protect_pages(va,1,prot))
                return (uint64_t)-ENOMEM;
        }
        uint64_t i=0;
        for (;i<pages;i++) {
            uint64_t va=addr+i*PAGE;
            if (!nas_mmu_user_byte(va) && !nas_mmu_map_prot_page(va,prot))
                break;
        }
        if (i!=pages) {
            /* A failed physical allocation leaves already-materialized
             * pages owned by the original VMA. No page-table alias is
             * created and the caller receives a genuine ENOMEM. */
            return (uint64_t)-ENOMEM;
        }
        TASK.mm->mmap_vmas=planned;
        return 0;
    }
    /* ELF/heap/stack are not yet represented by typed mmap VMA records.
     * Retain their proven PTE-only mprotect behavior until M85-B. */
    return nas_mmu_protect_pages(addr,pages,prot)?0:(uint64_t)-ENOMEM;
}
