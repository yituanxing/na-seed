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

#include "frame.h"
#define TASK (*nas_current_task)
/* M86 page replacement transaction. Stage new 4-KiB data frames outside
 * the live VM, followed by all required page-table directories.  Directory
 * preparation is allowed to allocate but must not change leaf mappings.
 * Commit only exchanges prevalidated leaves, cannot allocate or read files,
 * and releases former frame owners after TLB invalidation.
 *
 * A chained physical metadata frame stores up to 511 staged data frames;
 * no 64-MiB VLA or fixed-sized kernel-stack array is necessary. */
struct nas_vm_stage {
    uint64_t first,last;
    unsigned used;
};
static uint64_t *stage_metadata(uint64_t pa) {
    return (uint64_t *)(uintptr_t)(nas_hhdm_offset()+pa);
}
static bool stage_add(struct nas_vm_stage *s,uint64_t pa) {
    if(!s->last || s->used==511) {
        uint64_t metadata=nas_frame_alloc();
        if(!metadata)return false;
        if(s->last)stage_metadata(s->last)[0]=metadata;
        else s->first=metadata;
        s->last=metadata;
        s->used=0;
    }
    stage_metadata(s->last)[1+s->used++]=pa;
    return true;
}
static void stage_release(struct nas_vm_stage *s,bool release_data) {
    uint64_t at=s->first;
    while(at) {
        uint64_t *slots=stage_metadata(at);
        uint64_t next=slots[0];
        if(release_data)for(unsigned i=1;i<512;i++)
            if(slots[i] && !nas_frame_put(slots[i]))nas_arch_halt();
        if(!nas_frame_put(at))nas_arch_halt();
        at=next;
    }
    *s=(struct nas_vm_stage){0};
}
struct nas_vm_stage_cursor {uint64_t metadata;unsigned index;};
static uint64_t stage_next(struct nas_vm_stage_cursor *c) {
    if(!c->metadata)return 0;
    if(c->index==511) {
        c->metadata=stage_metadata(c->metadata)[0];
        c->index=0;
        if(!c->metadata)return 0;
    }
    return stage_metadata(c->metadata)[++c->index];
}
/* If this returns an errno, ALL original leaf PTEs and VMA policy remain
 * intact. May leave empty intermediate directories allocated after an OOM,
 * but those directories are owned by MM and reclaimed at mm_put. */
static uint64_t stage_replace_pages(uint64_t start,uint64_t pages,
                                    unsigned prot,bool file,uint64_t fd,
                                    uint64_t file_off,uint64_t length) {
    struct nas_vm_stage staged={0};
    uint8_t buffer[PAGE];
    if(prot)for(uint64_t i=0;i<pages;i++) {
        uint64_t pa=nas_frame_alloc();
        if(!pa) {stage_release(&staged,true);return (uint64_t)-ENOMEM;}
        if(file) {
            uint64_t length_here=length-i*PAGE;
            if(length_here>PAGE)length_here=PAGE;
            memset(buffer,0,sizeof(buffer));
            int64_t n=nas_fs_mmap_read(fd,file_off+i*PAGE,buffer,length_here);
            if(n<0) {
                (void)nas_frame_put(pa);
                stage_release(&staged,true);
                return (uint64_t)n;
            }
            memcpy((void *)(uintptr_t)(nas_hhdm_offset()+pa),buffer,PAGE);
        }
        if(!stage_add(&staged,pa)) {
            (void)nas_frame_put(pa);
            stage_release(&staged,true);
            return (uint64_t)-ENOMEM;
        }
    }
    /* Prepare all required page-table paths before exchanging a single
     * visible mapping. PROT_NONE only removes existing leaves, so do not
     * allocate directories for empty virtual reservations. */
    for(uint64_t i=0;i<pages;i++) {
        uint64_t va=start+i*PAGE;
        if(prot && !nas_mmu_prepare_leaf_at(TASK.mm->root_phys,va)) {
            stage_release(&staged,true);
            return (uint64_t)-ENOMEM;
        }
    }
    struct nas_vm_stage_cursor cursor={.metadata=staged.first,.index=0};
    for(uint64_t i=0;i<pages;i++) {
        uint64_t va=start+i*PAGE;
        uint64_t next_pa=prot?stage_next(&cursor):0;
        uint64_t old_pa=0;
        if(!prot) {
            /* No PTE means the logical PROT_NONE reservation already has
             * no materialized page. No need to manufacture directories. */
            if(!nas_mmu_user_byte(va))continue;
        }
        if(!nas_mmu_exchange_leaf_at(TASK.mm->root_phys,va,next_pa,prot,&old_pa))
            nas_arch_halt(); /* violated the successful PT preflight */
        if(old_pa && !nas_frame_put(old_pa))nas_arch_halt();
    }
    stage_release(&staged,false); /* PTEs now OWN all staged data frames */
    return 0;
}
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
#define heap_break TASK.mm->heap_break
/* Ordinary anonymous allocation stays in the top-down window. Fixed
 * mappings participate in the same per-task VM ownership ledger, including
 * the lower program/heap address domain used by original GCC + musl. */

/* M85-A: typed, independently MM-owned private mmap VMAs. Existing PT_LOAD and
 * heap/stack mappings stay in their established address domains. Releasing
 * PTE frames without returning the virtual range would eventually exhaust
 * the logical mmap windows even with abundant free physical RAM.
 * Dynamically sized VMA records are owned per MM: fork deep-clones them and
 * exec releases them after the new ELF image commits. COW/lazy faults are
 * handled by the shared MM owner; this VMA ledger remains authoritative.
 */
uint64_t nas_vm_anon_mmap(uint64_t hint, uint64_t length,
                         uint64_t prot, uint64_t flags,
                         uint64_t fd, uint64_t offset) {
    /* MAP_PRIVATE|MAP_ANONYMOUS. Linux ignores fd under MAP_ANONYMOUS:
     * an x86-64 libc may pass int -1 zero-extended to 0x00000000ffffffff.
     * Non-anonymous mappings and nonzero offsets remain explicitly refused. */
    /* A real upstream xz encoder can request a single anonymous allocation
     * larger than 64 MiB. The mmap VMA gap and physical-frame allocator
     * already enforce honest limits; an unrelated fixed 64-MiB syscall cap
     * incorrectly turns available RAM into an apparent malloc OOM. */
    if (!length || length > UINT64_MAX - PAGE)
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
        if (!nas_anon_fixed_range_allowed(hint,bytes))
            return (uint64_t)-EINVAL;
        /* Linux MAP_FIXED REPLACES a user mapping. It is not limited to
         * one previously created mmap VMA: libc may protect the brk/ELF
         * address domain, which is mapped by our existing shared loader.
         * Stage ledger changes first, and keep all kernel/stack VAs out. */
        struct nas_anon_regions planned={0};
        if(!nas_vma_clone(&planned,&TASK.mm->mmap_vmas))
            return (uint64_t)-ENOMEM;
        if (!nas_vma_replace(&planned,hint,bytes,(unsigned)prot,
                             NAS_VMA_ANON_PRIVATE,0)) {
            nas_anon_reset(&planned);
            return (uint64_t)-ENOMEM;
        }
        start=hint;
        uint64_t failure=stage_replace_pages(start,pages,(unsigned)prot,
                                              false,0,0,length);
        if(failure) { nas_anon_reset(&planned); return failure; }
        nas_vma_commit(&TASK.mm->mmap_vmas,&planned);
        return start;
    }
    if (!nas_anon_find_gap(&TASK.mm->mmap_vmas,bytes,&start))
        return (uint64_t)-ENOMEM;
    /* Stage logical policy BEFORE eager frame allocation. A full registry
     * cannot turn an OOM into a kernel halt after mapping physical memory. */
    struct nas_anon_regions planned={0};
    if(!nas_vma_clone(&planned,&TASK.mm->mmap_vmas))
        return (uint64_t)-ENOMEM;
    if (!nas_vma_record(&planned,start,bytes,(unsigned)prot,
                        NAS_VMA_ANON_PRIVATE,0)) {
        nas_anon_reset(&planned);
        return (uint64_t)-ENOMEM;
    }
    /* Linux private anonymous mmap owns its virtual range before any user
     * pages become resident.  Demand-zero materialization is already owned
     * by the common MM fault path on all three ISAs (and by kernel usercopy).
     * Keep both PROT_NONE and initially RW mappings nonresident here: musl
     * and Ruby reserve multi-megabyte thread stacks, then place PROT_NONE
     * guards before touching their interior. Eagerly creating every page
     * would destroy that legitimate no-backing guard transition. */
    nas_vma_commit(&TASK.mm->mmap_vmas,&planned);
    return start;
}
/* CPIO/tmpfs MAP_PRIVATE eagerly copies bytes into owned frames. Immutable
 * boot files may also satisfy read-only MAP_SHARED by copying: the provider
 * guarantees there can never be an observable subsequent file mutation.
 * Mutable MAP_SHARED still needs coherent backing and remains unsupported.
 * Fixed remaps may replace an existing ELF/heap user address range;
 * anonymous and file mappings use the same metadata and page ownership. */
uint64_t nas_vm_file_mmap(uint64_t hint, uint64_t length, uint64_t prot,
                          uint64_t flags, uint64_t fd, uint64_t offset) {
    const uint64_t MAP_SHARED=0x01ULL, MAP_PRIVATE=0x02ULL, MAP_FIXED=0x10ULL;
    if (!length || length > (64ULL<<20) ||
        length > UINT64_MAX-(PAGE-1) ||
        (offset & (PAGE-1)) || offset > UINT64_MAX-length)
        return (uint64_t)-EINVAL;
    bool shared=(flags & MAP_SHARED)!=0;
    bool private_map=(flags & MAP_PRIVATE)!=0;
    if ((flags & ~(MAP_PRIVATE|MAP_SHARED|MAP_FIXED)) ||
        (private_map == shared))
        return (uint64_t)-ENOSYS;
    if (!prot || (prot & ~7ULL)) return (uint64_t)-ENOSYS;
    if (!nas_vm_prot_supported(prot) || !nas_arch_user_nx_supported())
        return (uint64_t)-EOPNOTSUPP;
    int64_t valid=nas_fs_mmap_read(fd,offset,0,0);
    if (valid<0) return (uint64_t)valid;
    if(shared) {
        /* Read-only file/mount is insufficient: /proc is dynamic, and a
         * read-only mount may alias data mutable through another mount.
         * Do not acknowledge a writable shared mapping without real page
         * cache/writeback; mprotect must retain this restriction as well. */
        if(prot & 2ULL)return (uint64_t)-EACCES;
        if(prot!=1ULL)return (uint64_t)-ENOSYS;
        int immutable=nas_fs_mmap_immutable_readonly(fd);
        if(immutable<0)return (uint64_t)immutable;
        if(!immutable)return (uint64_t)-ENOSYS;
    }
    const unsigned kind=shared ? NAS_VMA_FILE_IMMUTABLE_SHARED :
                                 NAS_VMA_FILE_PRIVATE_SNAPSHOT;
    uint64_t count=(length+PAGE-1)/PAGE, bytes=count*PAGE;
    uint64_t start;
    struct nas_anon_regions planned={0};
    if(!nas_vma_clone(&planned,&TASK.mm->mmap_vmas))
        return (uint64_t)-ENOMEM;
    if (flags & MAP_FIXED) {
        if (!nas_anon_fixed_range_allowed(hint,bytes)) {
            nas_anon_reset(&planned);
            return (uint64_t)-EINVAL;
        }
        if (!nas_vma_replace(&planned,hint,bytes,(unsigned)prot,
                             kind,offset)) {
            nas_anon_reset(&planned);
            return (uint64_t)-ENOMEM;
        }
        start=hint;
    } else {
        if(!nas_anon_find_gap(&planned,bytes,&start)) {
            nas_anon_reset(&planned);
            return (uint64_t)-ENOMEM;
        }
        if(!nas_vma_record(&planned,start,bytes,(unsigned)prot,
                           kind,offset)) {
            nas_anon_reset(&planned);
            return (uint64_t)-ENOMEM;
        }
    }
    /* Stage all file bytes in private frames BEFORE disturbing an existing
     * MAP_FIXED mapping. Failed reads/OOM leave old contents unchanged.
     * Ordinary mmap is also staged outside its unallocated VMA window;
     * no eager anonymous placeholder or doubled physical memory is needed. */
    uint64_t failure=stage_replace_pages(start,count,(unsigned)prot,true,
                                         fd,offset,length);
    if(failure) { nas_anon_reset(&planned); return failure; }
    /* Whole mapping and its logical VMA become visible only after staging. */
    nas_vma_commit(&TASK.mm->mmap_vmas,&planned);
    return start;
}
uint64_t nas_vm_munmap(uint64_t addr, uint64_t len) {
    if (!len || (addr & (PAGE - 1)) || len > UINT64_MAX - (PAGE - 1))
        return (uint64_t)-EINVAL;
    uint64_t count = (len + PAGE - 1) / PAGE;
    if (addr >= USER_LIMIT || count > (USER_LIMIT - addr) / PAGE)
        return (uint64_t)-EINVAL;
    /* Plan a possibly partial unmap on a metadata copy first. If splitting
     * exhausts the available heap-backed VMA registry, leave metadata untouched. */
    struct nas_anon_regions updated={0};
    /* Stage changes to VMA metadata before committing any page changes. */
    if(!nas_vma_clone(&updated,&TASK.mm->mmap_vmas))
        return (uint64_t)-ENOMEM;
    if (!nas_anon_forget(&updated,addr,count*PAGE)) {
        nas_anon_reset(&updated);
        return (uint64_t)-ENOMEM;
    }
    for (uint64_t i=0; i<count; ++i) {
        uint64_t va=addr+i*PAGE;
        /* PROT_NONE reservations have intentionally absent PTEs. */
        if (nas_mmu_user_byte(va) && !nas_mmu_unmap_page(va)) {
            nas_anon_reset(&updated);
            return (uint64_t)-EFAULT;
        }
    }
    nas_vma_commit(&TASK.mm->mmap_vmas,&updated);
    return 0;
}
/* Linux MADV_DONTNEED discards *resident* private anonymous pages without
 * revoking the VMA. The next read/write fault observes the same permissions
 * and materializes a fresh zero page; COW frame refs are decremented, not
 * incorrectly modified in a sibling process. Snapshot-file VMAs have no
 * retained backing provider yet, so refusing them is essential: silently
 * zeroing a file page would violate Linux's read-back-on-demand semantics.
 *
 * MADV_DONTFORK/DOFORK are persistent VMA policies. They must affect both
 * child VMA ownership and fork COW leaf copies, not just syscall return. */
uint64_t nas_vm_madvise(uint64_t addr,uint64_t length,uint64_t advice) {
    const uint64_t MADV_DONTNEED=4U,MADV_DONTFORK=10U,MADV_DOFORK=11U;
    if((addr&(PAGE-1)) || length>UINT64_MAX-(PAGE-1))
        return (uint64_t)-EINVAL;
    if(advice!=MADV_DONTNEED && advice!=MADV_DONTFORK &&
       advice!=MADV_DOFORK)return (uint64_t)-EINVAL;
    if(!length)return 0;
    uint64_t pages=(length+PAGE-1)/PAGE;
    if(addr>=USER_LIMIT || pages>(USER_LIMIT-addr)/PAGE)
        return (uint64_t)-ENOMEM;
    uint64_t bytes=pages*PAGE;
    if(!nas_anon_covered(&TASK.mm->mmap_vmas,addr,bytes))
        return (uint64_t)-ENOMEM;
    if(advice==MADV_DONTFORK || advice==MADV_DOFORK) {
        struct nas_anon_regions planned={0};
        if(!nas_vma_clone(&planned,&TASK.mm->mmap_vmas))
            return (uint64_t)-ENOMEM;
        if(!nas_vma_set_dontfork(&planned,addr,bytes,
                                 advice==MADV_DONTFORK)) {
            nas_anon_reset(&planned);
            return (uint64_t)-ENOMEM;
        }
        nas_vma_commit(&TASK.mm->mmap_vmas,&planned);
        return 0;
    }
    /* Validate every VMA kind BEFORE dropping any page ownership. */
    const uint64_t end=addr+bytes;
    for(uint64_t at=addr;at<end;) {
        const struct nas_anon_region *v=nas_vma_find(&TASK.mm->mmap_vmas,at);
        if(!v || (v->kind!=NAS_VMA_ANON_PRIVATE &&
                  v->kind!=NAS_VMA_HEAP && v->kind!=NAS_VMA_STACK))
            return (uint64_t)-EOPNOTSUPP;
        at=v->end<end?v->end:end;
    }
    for(uint64_t at=addr;at<end;at+=PAGE) {
        uint64_t resident=0;
        if(nas_mmu_resolve_user_address(TASK.mm->root_phys,at,&resident,0) &&
           !nas_mmu_unmap_page(at))nas_arch_halt();
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
        /* A prior MAP_FIXED reservation must not be silently destroyed by
         * a later brk expansion: heap pages and mmap pages are disjoint. */
        if (nas_anon_intersects(&TASK.mm->mmap_vmas,before,after-before))
            return base;
        /* Heap VMA creation must be representable before physical mapping. */
        struct nas_anon_regions planned={0};
        if(!nas_vma_clone(&planned,&TASK.mm->mmap_vmas))return base;
        if(!nas_vma_record(&planned,before,after-before,3,NAS_VMA_HEAP,0)) {
            nas_anon_reset(&planned);
            return base;
        }
        uint64_t va=before;
        for (;va<after;va+=PAGE)
            if (!nas_mmu_map_page(va,true)) break;
        if (va!=after) {
            while (va>before) { va-=PAGE; if (!nas_mmu_unmap_page(va)) nas_arch_halt(); }
            nas_anon_reset(&planned);
            return base;
        }
        nas_vma_commit(&TASK.mm->mmap_vmas,&planned);
    } else if (after<before) {
        struct nas_anon_regions planned={0};
        if(!nas_vma_clone(&planned,&TASK.mm->mmap_vmas))return base;
        if (!nas_anon_forget(&planned,after,before-after)) {
            nas_anon_reset(&planned);
            return base;
        }
        for (uint64_t va=after;va<before;va+=PAGE)
            if (!nas_mmu_unmap_page(va)) { nas_anon_reset(&planned); return base; }
        nas_vma_commit(&TASK.mm->mmap_vmas,&planned);
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
    if (!prot) {
        /* A VMA-only, entirely nonresident interval can transition RW ->
         * PROT_NONE by changing logical permissions without losing data:
         * no physical backing exists to preserve.  This includes applying
         * PROT_NONE again to an existing guard.  For a resident page, never
         * discard its data frame or fake protection while the PTE remains
         * accessible: a real backing-preserving transition is still needed. */
        if(!nas_anon_covered(&TASK.mm->mmap_vmas,addr,pages*PAGE))
            return (uint64_t)-ENOMEM;
        /* File/ELF snapshots have eagerly copied backing. A PTE's absence
         * there is not authority to turn missing file data into anonymous
         * demand-zero memory; guard transitions only apply to zero-backed
         * VMAs and owned stack/heap reservations. */
        const uint64_t end=addr+pages*PAGE;
        for(uint64_t at=addr;at<end;) {
            const struct nas_anon_region *v=nas_vma_find(&TASK.mm->mmap_vmas,at);
            if(!v || (v->kind!=NAS_VMA_ANON_PRIVATE &&
                      v->kind!=NAS_VMA_HEAP && v->kind!=NAS_VMA_STACK))
                return (uint64_t)-ENOSYS;
            at=v->end<end?v->end:end;
        }
        for(uint64_t i=0;i<pages;i++) {
            uint64_t pa=0;
            if(nas_mmu_resolve_user_address(TASK.mm->root_phys,
                                           addr+i*PAGE,&pa,0))
                return (uint64_t)-ENOSYS;
        }
        struct nas_anon_regions planned={0};
        if(!nas_vma_clone(&planned,&TASK.mm->mmap_vmas))
            return (uint64_t)-ENOMEM;
        if(!nas_vma_protect(&planned,addr,pages*PAGE,0)) {
            nas_anon_reset(&planned);
            return (uint64_t)-ENOMEM;
        }
        nas_vma_commit(&TASK.mm->mmap_vmas,&planned);
        return 0;
    }
    if (!nas_vm_prot_supported(prot)) return (uint64_t)-EOPNOTSUPP;
    /* A permission change modifies the VMA policy and the permissions of
     * resident PTEs, NEVER the physical residency of a reservation. Musl's
     * pthread guards and V8's 512-MiB CodeRange are valid nonresident VMAs:
     * their pages materialize only on actual faults or kernel usercopy.
     * Enforce ownership before touching PTEs, including ELF and heap VMAs. */
    if (nas_anon_covered(&TASK.mm->mmap_vmas,addr,pages*PAGE)) {
        if(prot & NAS_VM_WRITE) {
            /* An immutable shared file cannot become writable by changing
             * PTEs after a read-only mmap. This must be rejected BEFORE
             * any staged VMA or physical page permissions are changed. */
            const uint64_t end=addr+pages*PAGE;
            for(uint64_t at=addr;at<end;) {
                const struct nas_anon_region *v=
                    nas_vma_find(&TASK.mm->mmap_vmas,at);
                if(!v)return (uint64_t)-ENOMEM;
                if(v->kind==NAS_VMA_FILE_IMMUTABLE_SHARED)
                    return (uint64_t)-EACCES;
                at=v->end<end?v->end:end;
            }
        }
        struct nas_anon_regions planned={0};
        if(!nas_vma_clone(&planned,&TASK.mm->mmap_vmas))
            return (uint64_t)-ENOMEM;
        if(!nas_vma_protect(&planned,addr,pages*PAGE,(unsigned)prot)) {
            nas_anon_reset(&planned);
            return (uint64_t)-ENOMEM;
        }
        /* M110: never create missing leaves. Their future fault path reads
         * the committed VMA prot. Present leaves retain frame identity,
         * and shared COW frames must stay hardware write-protected. */
        const uint64_t root=TASK.mm->root_phys;
        for(uint64_t i=0;i<pages;i++) {
            const uint64_t va=addr+i*PAGE;
            uint64_t pa=0;
            if(nas_mmu_resolve_user_address(root,va,&pa,0)) {
                /* Present PTEs and supported prot were prevalidated. A
                 * failure here means corruption, not recoverable ENOMEM. */
                unsigned hw_prot=(unsigned)prot;
                if((prot&NAS_VM_WRITE) &&
                   nas_frame_refs(pa&~UINT64_C(4095))>1)
                    hw_prot &= ~NAS_VM_WRITE;
                if(!nas_mmu_protect_pages(va,1,hw_prot))
                    nas_arch_halt();
            }
        }
        nas_vma_commit(&TASK.mm->mmap_vmas,&planned);
        return 0;
    }
    /* Never grant hardware write access to a leaf outside MM's VMA ledger.
     * Such a mapping has no recoverable COW/fault policy; keeping the old
     * PTE-only fallback would make mprotect an ownership bypass. */
    return (uint64_t)-ENOMEM;
}
