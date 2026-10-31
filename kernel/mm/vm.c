#include <stdint.h>
#include "../errno.h"
#include "../task.h"
#include "../arch/arch.h"
#include "vm.h"
#include "mm.h"
#include "prot.h"
#include "anon_regions.h"
#include "file_backing.h"
#include "../lib/string.h"
#include "../fs/io.h"
#include "../arch/mmu.h"
#include "../arch/user_pt.h"
#include "mmap_abi.h"

#include "frame.h"
#define TASK (*nas_current_task)
static bool mutable_shared(const struct nas_anon_region *v) {
    return v && v->kind==NAS_VMA_FILE_MUTABLE_SHARED && v->backing;
}
/* Complete the backing operation before committing a VMA replacement.
 * The cache is inode-scoped, so one flush covers overlapping aliases.
 * Propagate provider errors; a MAP_FIXED/munmap must not silently throw
 * away data that can no longer be reached through its old mapping. */
static int sync_shared_intersection(const struct nas_anon_regions *regions,
                                    uint64_t start,uint64_t length,bool rearm) {
    if(!regions || length>UINT64_MAX-start)return -EINVAL;
    uint64_t end=start+length;
    for(unsigned i=0;i<regions->capacity;i++) {
        const struct nas_anon_region *v=&regions->slots[i];
        if(!mutable_shared(v) || start>=v->end || end<=v->start)continue;
        int rc=nas_fs_mmap_sync(v->backing,rearm);
        if(rc)return rc;
    }
    return 0;
}
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
    bool shared_anon=nas_linux_anon_shared_map_supported(flags,offset);
    if (!fixed && !shared_anon && !nas_linux_anon_map_supported(flags,fd,offset))
        return (uint64_t)-ENOSYS;
    if (prot & ~7ULL) return (uint64_t)-EINVAL;
    if (prot && !nas_vm_prot_supported(prot)) return (uint64_t)-EOPNOTSUPP;
    if (!nas_arch_user_nx_supported()) return (uint64_t)-ENOSYS;
    /* Shared anonymous storage has no file cache to fault in after fork.
     * Materialize its bounded page backing before publication, preserving
     * actual physical aliases in both parent and child.  Refuse unsupported
     * nonresident/huge forms rather than silently switching them to COW. */
    if(shared_anon && (!prot || length>(64ULL<<20)))
        return (uint64_t)-EOPNOTSUPP;
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
        int sync=sync_shared_intersection(&TASK.mm->mmap_vmas,start,bytes,false);
        if(sync) {nas_anon_reset(&planned);return (uint64_t)(int64_t)sync;}
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
                        shared_anon?NAS_VMA_ANON_SHARED:NAS_VMA_ANON_PRIVATE,0)) {
        nas_anon_reset(&planned);
        return (uint64_t)-ENOMEM;
    }
    if(shared_anon) {
        uint64_t error=stage_replace_pages(start,pages,(unsigned)prot,
                                           false,0,0,length);
        if(error) {nas_anon_reset(&planned);return error;}
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
/* Small MAP_PRIVATE remains an eager independent file snapshot. Large
 * MAP_PRIVATE of an immutable source retains a pinned inode and reads into
 * private frames on demand: musl maps >100-MiB signed libLLVM objects and
 * must not consume hundreds of MiB before touching any library page.
 * Mutable MAP_SHARED retains coherent cache frames on demand. All pinned
 * mappings survive close/rename/unlink/fork. */
uint64_t nas_vm_file_mmap(uint64_t hint, uint64_t length, uint64_t prot,
                          uint64_t flags, uint64_t fd, uint64_t offset) {
    const uint64_t MAP_SHARED=0x01ULL, MAP_PRIVATE=0x02ULL, MAP_FIXED=0x10ULL;
    if (!length ||
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
    bool live_shared=false, lazy_private=false;
    struct nas_mapped_file *backing=NULL;
    if(shared) {
        /* An immutable provider is permitted to share an eager read-only
         * snapshot. A writable/mutable source MUST use a live cache, never
         * the old private copy. The inode pin is acquired before any VMA
         * mutation, and independently outlives the file descriptor. */
        int immutable=nas_fs_mmap_immutable_readonly(fd);
        if(immutable<0)return (uint64_t)(int64_t)immutable;
        if(immutable && (prot&NAS_VM_WRITE))return (uint64_t)-EACCES;
        if(!immutable || prot!=NAS_VM_READ) {
            int pinned=nas_fs_mmap_pin(fd,(prot&NAS_VM_WRITE)!=0,&backing);
            if(pinned)return (uint64_t)(int64_t)pinned;
            live_shared=true;
        }
    }
    /* Eager file snapshots are intentionally bounded. Only an immutable
     * private source can exceed this threshold without a full copy: pin its
     * VFS identity and materialize page-sized private snapshots on fault. */
    if(!live_shared && length>(64ULL<<20)) {
        if(private_map) {
            int pinned=nas_fs_mmap_pin_immutable(fd,&backing);
            if(!pinned)lazy_private=true;
            else return (uint64_t)(int64_t)pinned;
        } else return (uint64_t)-EINVAL;
    }
    const unsigned kind=live_shared ? NAS_VMA_FILE_MUTABLE_SHARED :
                         (lazy_private ? NAS_VMA_FILE_PRIVATE_LAZY :
                         (shared ? NAS_VMA_FILE_IMMUTABLE_SHARED :
                                   NAS_VMA_FILE_PRIVATE_SNAPSHOT));
    const bool lazy_backed=live_shared || lazy_private;
    uint64_t count=(length+PAGE-1)/PAGE, bytes=count*PAGE;
    uint64_t start;
    struct nas_anon_regions planned={0};
    if(!nas_vma_clone(&planned,&TASK.mm->mmap_vmas)) {
        if(backing)nas_fs_mmap_put(backing);
        return (uint64_t)-ENOMEM;
    }
    if (flags & MAP_FIXED) {
        if (!nas_anon_fixed_range_allowed(hint,bytes)) {
            nas_anon_reset(&planned);
            if(backing)nas_fs_mmap_put(backing);
            return (uint64_t)-EINVAL;
        }
        bool recorded=live_shared ?
            nas_vma_replace_shared(&planned,hint,bytes,(unsigned)prot,
                                   offset,backing) :
            (lazy_private ? nas_vma_replace_backed(&planned,hint,bytes,
                                                  (unsigned)prot,kind,offset,
                                                  backing) :
                            nas_vma_replace(&planned,hint,bytes,
                                            (unsigned)prot,kind,offset));
        if (!recorded) {
            nas_anon_reset(&planned);
            if(backing)nas_fs_mmap_put(backing);
            return (uint64_t)-ENOMEM;
        }
        start=hint;
    } else {
        if(!nas_anon_find_gap(&planned,bytes,&start)) {
            nas_anon_reset(&planned);
            if(backing)nas_fs_mmap_put(backing);
            return (uint64_t)-ENOMEM;
        }
        bool recorded=live_shared ?
            nas_vma_record_shared(&planned,start,bytes,(unsigned)prot,
                                  offset,backing) :
            (lazy_private ? nas_vma_record_backed(&planned,start,bytes,
                                                 (unsigned)prot,kind,offset,
                                                 backing) :
                            nas_vma_record(&planned,start,bytes,
                                           (unsigned)prot,kind,offset));
        if(!recorded) {
            nas_anon_reset(&planned);
            if(backing)nas_fs_mmap_put(backing);
            return (uint64_t)-ENOMEM;
        }
    }
    /* MAP_FIXED first synchronizes displaced dirty shared pages. Both
     * shared-live and immutable-private-lazy VMAs stay nonresident; eager
     * snapshots preserve the original all-or-nothing data transaction. */
    uint64_t failure=0;
    if(flags & MAP_FIXED) {
        int sync=sync_shared_intersection(&TASK.mm->mmap_vmas,start,bytes,false);
        if(sync)failure=(uint64_t)(int64_t)sync;
    }
    if(!failure && (flags & MAP_FIXED || !lazy_backed))
        failure=stage_replace_pages(start,count,
                                    lazy_backed?0:(unsigned)prot,
                                    !lazy_backed,fd,offset,length);
    if(failure) {
        nas_anon_reset(&planned);
        if(backing)nas_fs_mmap_put(backing);
        return failure;
    }
    nas_vma_commit(&TASK.mm->mmap_vmas,&planned);
    if(backing)nas_fs_mmap_put(backing); /* ledger now owns its own ref */
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
    int sync=sync_shared_intersection(&TASK.mm->mmap_vmas,addr,count*PAGE,false);
    if(sync) {nas_anon_reset(&updated);return (uint64_t)(int64_t)sync;}
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
/* Linux msync accepts flags=0 (historically equivalent to ASYNC), but
 * neither unknown bits nor simultaneous ASYNC/SYNC are valid. Anonymous
 * and private snapshot mappings have no file writeback; live shared VMAs
 * own a pinned address-space whose cache must flush real backing bytes.
 * Fail on any VMA hole, including a hole between two valid fragments. */
uint64_t nas_vm_msync(uint64_t addr,uint64_t length,uint64_t flags) {
    const uint64_t MS_ASYNC=1ULL,MS_INVALIDATE=2ULL,MS_SYNC=4ULL;
    if((addr&(PAGE-1)) ||
       (flags&~(MS_ASYNC|MS_INVALIDATE|MS_SYNC)) ||
       ((flags&MS_ASYNC) && (flags&MS_SYNC)))
        return (uint64_t)-EINVAL;
    if(!length)return 0;
    if(length>UINT64_MAX-(PAGE-1))return (uint64_t)-ENOMEM;
    uint64_t pages=(length+PAGE-1)/PAGE;
    if(addr>=USER_LIMIT || pages>(USER_LIMIT-addr)/PAGE)
        return (uint64_t)-ENOMEM;
    if(!nas_anon_covered(&TASK.mm->mmap_vmas,addr,pages*PAGE))
        return (uint64_t)-ENOMEM;
    /* Invalidation needs a coherent page-cache eviction protocol, including
     * other address spaces. Explicitly reject it until that mechanism is
     * available; never acknowledge a no-op as MS_INVALIDATE success. */
    if(flags&MS_INVALIDATE)return (uint64_t)-EOPNOTSUPP;
    int rc=sync_shared_intersection(&TASK.mm->mmap_vmas,addr,pages*PAGE,true);
    return (uint64_t)(int64_t)rc;
}
/* Linux MADV_DONTNEED discards resident private anonymous pages (next fault
 * reads zero) and immutable-private lazy file pages (next fault re-reads the
 * original pinned file). Eager snapshot-file VMAs have no retained backing,
 * so refusing those is essential: zeroing a file page violates semantics.
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
                  v->kind!=NAS_VMA_HEAP && v->kind!=NAS_VMA_STACK &&
                  v->kind!=NAS_VMA_FILE_PRIVATE_LAZY))
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
        if(sync_shared_intersection(&TASK.mm->mmap_vmas,
                                    after,before-after,false)) {
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
        /* Eager file/ELF snapshots have no backing if resident PTEs are
         * discarded. Live shared file mappings can dematerialize safely;
         * immutable private lazy mappings can transition only while no
         * privately modified page has become resident. */
        const uint64_t end=addr+pages*PAGE;
        for(uint64_t at=addr;at<end;) {
            const struct nas_anon_region *v=nas_vma_find(&TASK.mm->mmap_vmas,at);
            if(!v || (v->kind!=NAS_VMA_ANON_PRIVATE &&
                      v->kind!=NAS_VMA_HEAP && v->kind!=NAS_VMA_STACK &&
                      !mutable_shared(v) &&
                      v->kind!=NAS_VMA_FILE_PRIVATE_LAZY))
                return (uint64_t)-ENOSYS;
            at=v->end<end?v->end:end;
        }
        for(uint64_t i=0;i<pages;i++) {
            uint64_t va=addr+i*PAGE;
            uint64_t pa=0;
            const struct nas_anon_region *v=
                nas_vma_find(&TASK.mm->mmap_vmas,va);
            if(nas_mmu_resolve_user_address(TASK.mm->root_phys,va,&pa,0) &&
               !mutable_shared(v))
                return (uint64_t)-ENOSYS;
        }
        struct nas_anon_regions planned={0};
        if(!nas_vma_clone(&planned,&TASK.mm->mmap_vmas))
            return (uint64_t)-ENOMEM;
        if(!nas_vma_protect(&planned,addr,pages*PAGE,0)) {
            nas_anon_reset(&planned);
            return (uint64_t)-ENOMEM;
        }
        for(uint64_t i=0;i<pages;i++) {
            uint64_t va=addr+i*PAGE;
            const struct nas_anon_region *v=
                nas_vma_find(&TASK.mm->mmap_vmas,va);
            uint64_t pa=0;
            if(mutable_shared(v) &&
               nas_mmu_resolve_user_address(TASK.mm->root_phys,va,&pa,0) &&
               !nas_mmu_unmap_page(va))nas_arch_halt();
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
                if(mutable_shared(v) && !nas_fs_mmap_writable(v->backing))
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
                const struct nas_anon_region *v=
                    nas_vma_find(&TASK.mm->mmap_vmas,va);
                /* Shared pages must NEVER COW merely because a cache and
                 * another process hold frame refs. Re-arm write trapping
                 * instead: the first genuine store marks inode times and
                 * advances the page's coherent dirty state. */
                if(mutable_shared(v))hw_prot &= ~NAS_VM_WRITE;
                else if(v && v->kind==NAS_VMA_ANON_SHARED)
                    hw_prot=(unsigned)prot; /* true shared physical frame */
                else if((prot&NAS_VM_WRITE) &&
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
struct nas_vm_resize_context {
    const struct nas_fs_node *node;
    uint64_t new_size;
};
/* A truncate affects every process mapping this inode, not just the Task
 * issuing ftruncate(2). The VFS lease pins an incarnation across close,
 * unlink and rename; an inode-number-only comparison would be unsound. */
static bool invalidate_mm_after_resize(struct nas_mm *mm,void *opaque) {
    const struct nas_vm_resize_context *ctx=opaque;
    for(unsigned i=0;i<mm->mmap_vmas.capacity;i++) {
        const struct nas_anon_region *v=&mm->mmap_vmas.slots[i];
        if(!mutable_shared(v) ||
           !nas_fs_mmap_matches(v->backing,ctx->node))continue;
        for(uint64_t va=v->start;va<v->end;va+=PAGE) {
            uint64_t off=v->file_offset+(va-v->start);
            uint64_t pa=0;
            if(!nas_mm_resolve_user(mm,va,&pa,0))continue;
            if(off>=ctx->new_size) {
                uint64_t old=0;
                if(!nas_mmu_exchange_leaf_at(mm->root_phys,va,0,0,&old) ||
                   old!=(pa&~(PAGE-1)))nas_arch_halt();
                if(!nas_frame_put(old))nas_arch_halt();
            } else if(ctx->new_size-off<PAGE) {
                /* Linux zeroes the tail of the last partial page after
                 * truncate, including every already-materialized alias. */
                uint64_t first=ctx->new_size-off;
                memset((void *)(uintptr_t)(nas_hhdm_offset()+
                       (pa&~(PAGE-1))+first),0,(size_t)(PAGE-first));
            }
        }
    }
    return true;
}
void nas_vm_invalidate_file(const struct nas_fs_node *node,uint64_t new_size) {
    if(!node)return;
    struct nas_vm_resize_context ctx={node,new_size};
    if(!nas_mm_walk_live(invalidate_mm_after_resize,&ctx))nas_arch_halt();
}
/* Dirty-state observation needs a re-arm boundary. Without one, a process
 * can write the same writable shared leaf for the rest of its lifetime
 * without another page fault, leaving mtime stale after a subsequent sync.
 * Single-CPU NaSeed has no concurrent user writes during this pass. */
static bool rearm_mm_shared_file(struct nas_mm *mm,void *opaque) {
    const struct nas_fs_node *node=opaque;
    for(unsigned i=0;i<mm->mmap_vmas.capacity;i++) {
        const struct nas_anon_region *v=&mm->mmap_vmas.slots[i];
        if(!mutable_shared(v) || !(v->prot&NAS_VM_WRITE) ||
           !nas_fs_mmap_matches(v->backing,node))continue;
        for(uint64_t va=v->start;va<v->end;va+=PAGE) {
            uint64_t physical=0;
            unsigned hw=0;
            if(!nas_mm_resolve_user(mm,va,&physical,&hw) ||
               !(hw&NAS_UP_WRITE))continue;
            uint64_t frame=physical&~(PAGE-1);
            uint64_t old=0;
            /* Existing leaf+directory: no allocation, COW or frame ref
             * transfer. Preserve R/X, only clear the hardware W bit. */
            unsigned rearmed=(v->prot|NAS_VM_READ)&~NAS_VM_WRITE;
            if(!nas_mmu_exchange_leaf_at(mm->root_phys,va,frame,rearmed,&old) ||
               old!=frame)nas_arch_halt();
        }
    }
    return true;
}
void nas_vm_rearm_file(const struct nas_fs_node *node) {
    if(node && !nas_mm_walk_live(rearm_mm_shared_file,(void *)node))
        nas_arch_halt();
}
