#include "mm.h"
#include "../arch/mmu.h"
#include "../panic.h"
#include "../lib/string.h"
#include "../task.h"
#include "frame.h"
#include "prot.h"
#include "../arch/user_pt.h"

/* Single-CPU fixed-capacity owner pool. Allocation policy is private to
 * the MM subsystem; Task must never become the storage owner again. */
static struct nas_mm mm_pool[NAS_TASK_LIMIT * 2U]; /* spare replacement MMs during exec */

void nas_mm_system_init(void) {
    memset(mm_pool, 0, sizeof(mm_pool));
}
struct nas_mm *nas_mm_new(void) {
    for (unsigned n=0; n<NAS_TASK_LIMIT * 2U; ++n) {
        struct nas_mm *mm=&mm_pool[n];
        if (mm->users) continue;
        memset(mm, 0, sizeof(*mm));
        mm->users=1;
        return mm;
    }
    return 0;
}
bool nas_mm_walk_live(nas_mm_live_visit visit,void *ctx) {
    if(!visit)return false;
    for(unsigned n=0;n<NAS_TASK_LIMIT*2U;n++) {
        struct nas_mm *mm=&mm_pool[n];
        if(mm->users && mm->root_phys && !visit(mm,ctx))return false;
    }
    return true;
}
/* The platform page tables are private; only ordinary user data frames
 * become shared. Every user leaf must have a logical VMA owner. Reject an
 * orphan leaf rather than silently reviving the old eager-fork fallback:
 * such a child could never resolve a later permission/COW fault correctly.
 * Do not change ANY parent PTE until the full child walk succeeds. */
struct cow_fork_ctx {
    struct nas_mm *dst;
    const struct nas_mm *src;
};
static unsigned access_prot(unsigned access) {
    return NAS_VM_READ|((access&NAS_UP_WRITE)?NAS_VM_WRITE:0)|
                       ((access&NAS_UP_EXEC)?NAS_VM_EXEC:0);
}
static bool cow_clone_mapping(uint64_t va,unsigned access,void *opaque) {
    struct cow_fork_ctx *ctx=opaque;
    uint64_t pa=0;
    if(!nas_mm_resolve_user(ctx->src,va,&pa,0))return false;
    pa &= ~UINT64_C(4095);
    const struct nas_anon_region *v=nas_vma_find(&ctx->src->mmap_vmas,va);
    if(!v || ((access&NAS_UP_WRITE) && !(v->prot&NAS_VM_WRITE)))return false;
    /* MADV_DONTFORK excludes both its VMA and resident PTEs from child MM.
     * Parent remains the sole owner: do not acquire a second frame ref. */
    if(v->flags&NAS_VMA_DONTFORK)return true;
    if(!nas_frame_get(pa))return false;
    /* Shared file mappings retain the exact same backing frame and Linux
     * write permission across fork.  Only MAP_PRIVATE pages become COW. */
    unsigned prot=access_prot(access);
    if(v->kind!=NAS_VMA_FILE_MUTABLE_SHARED)prot &= ~NAS_VM_WRITE;
    if(!nas_mmu_prepare_leaf_at(ctx->dst->root_phys,va)) {
        (void)nas_frame_put(pa);
        return false;
    }
    uint64_t prior=0;
    if(!nas_mmu_exchange_leaf_at(ctx->dst->root_phys,va,pa,prot,&prior)) {
        (void)nas_frame_put(pa);
        return false;
    }
    if(prior)nas_fail("COW fork: child page already mapped");
    return true;
}
static bool cow_protect_parent(uint64_t va,unsigned access,void *opaque) {
    const struct nas_mm *src=opaque;
    if(!(access&NAS_UP_WRITE))return true;
    const struct nas_anon_region *v=nas_vma_find(&src->mmap_vmas,va);
    if(!v || !(v->prot&NAS_VM_WRITE))return false;
    /* Excluded pages were not cloned and must not be write-protected. */
    if((v->flags&NAS_VMA_DONTFORK) ||
       v->kind==NAS_VMA_FILE_MUTABLE_SHARED)return true;
    uint64_t pa=0;
    if(!nas_mm_resolve_user(src,va,&pa,0))return false;
    pa &= ~UINT64_C(4095);
    uint64_t old=0;
    if(!nas_mmu_exchange_leaf_at(src->root_phys,va,pa,
                                 access_prot(access)&~NAS_VM_WRITE,&old) ||
       old!=pa) return false;
    return true; /* same data frame; ownership unchanged */
}
struct nas_mm *nas_mm_fork_cow(const struct nas_mm *source) {
    if(!source || !source->users || !source->root_phys)
        nas_fail("COW fork: invalid source");
    struct nas_mm *dst=nas_mm_new();
    if(!dst)return 0;
    dst->root_phys=nas_mmu_alloc_root();
    if(!dst->root_phys){nas_mm_put(dst);return 0;}
    dst->heap_break=source->heap_break;
    /* Fork inherits the *policies* but has an independent ledger allocation:
     * a child munmap/mprotect must never mutate or free its parent's slots. */
    if(!nas_vma_clone(&dst->mmap_vmas,&source->mmap_vmas)) {
        nas_mm_put(dst);
        return 0;
    }
    /* Remove excluded policy intervals BEFORE creating child PTEs. The
     * operation forgets complete source VMAs; no split/heap allocation is
     * needed, and a child may demand-fault only truly inherited regions. */
    for(unsigned i=0;i<source->mmap_vmas.capacity;i++) {
        const struct nas_anon_region *v=&source->mmap_vmas.slots[i];
        if(v->end && (v->flags&NAS_VMA_DONTFORK) &&
           !nas_anon_forget(&dst->mmap_vmas,v->start,v->end-v->start)) {
            nas_mm_put(dst);
            return 0;
        }
    }
    struct cow_fork_ctx ctx={dst,source};
    if(!nas_mm_walk_pages(source,cow_clone_mapping,&ctx)) {
        nas_mm_put(dst); /* decrement acquired frame refs, parent untouched */
        return 0;
    }
    /* No allocation or rollback can fail after this commit barrier: source
     * mappings were walked and the parent PTE paths were already present.
     * A failed exchange now is a genuine kernel invariant violation. */
    if(!nas_mm_walk_pages(source,cow_protect_parent,(void *)source))
        nas_fail("COW fork: parent write-protection invariant");
    return dst;
}
/* Kernel/ptrace writes must privatize a shared page even when its user
 * mapping is RX, without silently granting WRITE permission to userspace.
 * A page with a single remaining owner can be promoted in-place. */
bool nas_mm_break_cow(struct nas_mm *mm,uint64_t va,
                      unsigned logical_prot,bool preserve_perm) {
    /* A shared-file page has a separate inode/cache owner.  Its frame's
     * multiple references mean live aliases, not a private COW request.
     * Callers must preserve this mapping instead of copying its bytes. */
    const struct nas_anon_region *v=mm ?
        nas_vma_find(&mm->mmap_vmas,va) : 0;
    if(v && v->kind==NAS_VMA_FILE_MUTABLE_SHARED)return false;
    uint64_t pa=0;
    unsigned access=0;
    if(!nas_mm_resolve_user(mm,va,&pa,&access))return false;
    uint64_t old_frame=pa&~UINT64_C(4095);
    uint32_t refs=nas_frame_refs(old_frame);
    if(!refs)return false;
    unsigned prot=preserve_perm ? access_prot(access) : logical_prot;
    if(refs==1) {
        if(preserve_perm || (access&NAS_UP_WRITE))return true;
        uint64_t old=0;
        return nas_mmu_exchange_leaf_at(mm->root_phys,va&~UINT64_C(4095),
                                        old_frame,prot,&old) && old==old_frame;
    }
    uint64_t copy=nas_frame_alloc();
    if(!copy)return false;
    memcpy((void *)(uintptr_t)(nas_hhdm_offset()+copy),
           (const void *)(uintptr_t)(nas_hhdm_offset()+old_frame),4096);
    uint64_t old=0;
    if(!nas_mmu_exchange_leaf_at(mm->root_phys,va&~UINT64_C(4095),
                                 copy,prot,&old)) {
        (void)nas_frame_put(copy);
        return false;
    }
    if(old!=old_frame || !nas_frame_put(old))
        nas_fail("COW: mapping/frame refcount ownership mismatch");
    return true;
}
bool nas_mm_walk_pages(const struct nas_mm *mm,nas_mm_page_visit visit,void *ctx) {
    if(!mm || !mm->users || !mm->root_phys || !visit)return false;
    return nas_mmu_walk_user_mapping(mm->root_phys,visit,ctx);
}
bool nas_mm_resolve_user(const struct nas_mm *mm,uint64_t va,
                         uint64_t *physical,unsigned *access) {
    if(!mm || !mm->users || !mm->root_phys || !physical)return false;
    return nas_mmu_resolve_user_address(mm->root_phys,va,physical,access);
}
struct nas_mm *nas_mm_get(struct nas_mm *mm) {
    if (!mm || !mm->users || mm->users==UINT32_MAX)
        nas_fail("MM get: invalid or saturated reference");
    ++mm->users;
    return mm;
}
void nas_mm_put(struct nas_mm *mm) {
    if (!mm || !mm->users)
        nas_fail("MM put: zero or invalid reference");
    if (--mm->users) return;
    if (mm->root_phys && !nas_mmu_reclaim_root(mm->root_phys))
        nas_fail("MM final put: failed page-table reclamation");
    nas_anon_reset(&mm->mmap_vmas);
    memset(mm, 0, sizeof(*mm));
}
