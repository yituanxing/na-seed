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
#define TASK (*nas_current_task)
#define heap_break TASK.heap_break
static bool vm_owned_pages(uint64_t start, uint64_t length);

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
        /* MAP_FIXED must replace pages only in an existing reservation.
         * Never trample kernel/ELF/stack mappings outside the VMA owner. */
        if ((hint & (PAGE-1)) || !vm_owned_pages(hint,bytes))
            return (uint64_t)-ENOMEM;
        start=hint;
        for (uint64_t va=start;va<start+bytes;va+=PAGE) {
            if (nas_mmu_user_byte(va) && !nas_mmu_unmap_page(va))
                return (uint64_t)-EFAULT;
            if (prot && !nas_mmu_map_prot_page(va,prot))
                return (uint64_t)-ENOMEM;
        }
        return start;
    }
    if (!nas_anon_find_gap(&TASK.anonymous,bytes,&start))
        return (uint64_t)-ENOMEM;
    /* PROT_NONE is a real reserved virtual interval with no PTE or
     * allocated physical page. musl can populate it later using MAP_FIXED. */
    uint64_t va=start;
    if (prot) {
        for (;va<start+bytes;va+=PAGE)
            if (!nas_mmu_map_prot_page(va,prot)) break;
        if (va != start+bytes) {
            while (va>start) { va-=PAGE; if (!nas_mmu_unmap_page(va)) nas_arch_halt(); }
            return (uint64_t)-ENOMEM;
        }
    }
    if (!nas_anon_record(&TASK.anonymous,start,bytes))
        nas_arch_halt();
    return start;
}
/* A CPIO/tmpfs regular file participates in genuine Linux MAP_PRIVATE.
 * An initial mapping allocates owned, eager physical pages; a MAP_FIXED
 * remap overlays only an already reserved VMA, as musl's DSO loader does.
 * No mmap() call changes the shared FD's read/seek offset. */
static bool vm_owned_pages(uint64_t start, uint64_t length) {
    if (start < NAS_ANON_FLOOR || start > NAS_ANON_CEILING ||
        length > NAS_ANON_CEILING-start) return false;
    uint64_t end=start+length;
    for (uint64_t va=start;va<end;va+=PAGE) {
        bool covered=false;
        for (unsigned i=0;i<NAS_ANON_REGION_LIMIT;i++) {
            const struct nas_anon_region *r=&TASK.anonymous.slots[i];
            if (r->end && va>=r->start && va<r->end) {
                covered=true;
                break;
            }
        }
        if (!covered) return false;
    }
    return true;
}
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
    if (flags & MAP_FIXED) {
        if ((hint & (PAGE-1)) || hint>=USER_LIMIT) return (uint64_t)-EINVAL;
        if (!vm_owned_pages(hint,bytes)) return (uint64_t)-ENOMEM;
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
            if (!nas_mmu_user_byte(va) &&
                !nas_mmu_map_prot_page(va,prot))
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
    memcpy(&updated, &TASK.anonymous, sizeof(updated));
    if (!nas_anon_forget(&updated,addr,count*PAGE))
        return (uint64_t)-ENOMEM;
    for (uint64_t i=0; i<count; ++i) {
        uint64_t va=addr+i*PAGE;
        /* PROT_NONE reservations have intentionally absent PTEs. */
        if (nas_mmu_user_byte(va) && !nas_mmu_unmap_page(va))
            return (uint64_t)-EFAULT;
    }
    memcpy(&TASK.anonymous, &updated, sizeof(updated));
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
