#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../kernel/mm/anon_regions.h"
#include "../kernel/arch/abi.h"
#include "../kernel/task/user_stack.h"
#define P 4096ULL
/* Only the file owner API is mocked: this fixture executes production VMA
 * clone/split/reprotect/replace/release with real reference transitions. */
struct nas_mapped_file { unsigned refs; };
static bool deny_file_get;
bool nas_fs_mmap_get(struct nas_mapped_file *file) {
    if(!file || !file->refs || deny_file_get)return false;
    file->refs++;
    return true;
}
void nas_fs_mmap_put(struct nas_mapped_file *file) {
    assert(file && file->refs);
    file->refs--;
}
static bool ordered(const struct nas_anon_region *r,void *ctx) {
    uint64_t *previous=(uint64_t *)ctx;
    assert(r->start>=*previous && r->end>r->start);
    *previous=r->end;
    return true;
}
int main(void) {
    struct nas_anon_regions a={0},b={0};
    uint64_t x=0,y=0,z=0;
    assert(nas_anon_find_gap(&a,3*P,&x));
    assert(x==NAS_ANON_CEILING-3*P);
    assert(nas_anon_record(&a,x,3*P));
    assert(nas_anon_find_gap(&a,P,&y));
    assert(y==x-P && nas_anon_record(&a,y,P));
    assert(nas_anon_forget(&a,x+P,P));
    assert(nas_anon_find_gap(&a,P,&z) && z==x+P);
    assert(nas_anon_record(&a,z,P));
    assert(nas_vma_clone(&b,&a));
    assert(nas_anon_forget(&b,x,3*P));
    assert(nas_anon_find_gap(&b,3*P,&z) && z==x);
    assert(nas_anon_find_gap(&a,P,&z) && z==y-P);
    assert(nas_anon_forget(&a,0x10000000,P));
    assert(nas_anon_find_gap(&a,P,&z) && z==y-P);
    assert(!nas_anon_record(&a,x,3*P));
    assert(!nas_anon_record(&a,x+1,P));
    assert(!nas_anon_find_gap(&a,UINT64_MAX,&z));
    assert(!nas_anon_forget(&a,UINT64_MAX-1023,2*P));
    /* Original Node/V8 reserves multiple disjoint CodeRanges totaling over
     * the historic 1-GiB mmap window. A full low region must spill into the
     * higher hardware-safe 64-bit VA window, without touching the fixed
     * stack/trampoline gap at 0x60000000..0x80000000. */
    struct nas_anon_regions high_test={0};
    assert(nas_vma_record(&high_test,NAS_ANON_FLOOR,
                          NAS_ANON_CEILING-NAS_ANON_FLOOR,0,
                          NAS_VMA_ANON_PRIVATE,0));
    assert(nas_anon_find_gap(&high_test,P,&z) &&
           z==NAS_ANON_EXTENDED_CEILING-P);
    assert(nas_vma_record(&high_test,z,P,0,NAS_VMA_ANON_PRIVATE,0));
    assert(nas_anon_find_gap(&high_test,
                  NAS_ANON_CEILING-NAS_ANON_FLOOR+P,&z) &&
           z>=NAS_ANON_EXTENDED_FLOOR);
    assert(!nas_anon_find_gap(&high_test,
                  NAS_ANON_EXTENDED_CEILING-NAS_ANON_EXTENDED_FLOOR+P,&z));
    assert(nas_anon_fixed_range_allowed(NAS_ANON_FIXED_FLOOR,P));
    assert(nas_anon_fixed_range_allowed(NAS_ANON_EXTENDED_FLOOR,P));
    assert(nas_anon_fixed_range_allowed(NAS_ANON_EXTENDED_CEILING-P,P));
    assert(!nas_anon_fixed_range_allowed(NAS_ANON_CEILING-P,2*P));
    assert(!nas_anon_fixed_range_allowed(0x6ff00000,P));
    assert(!nas_anon_fixed_range_allowed(NAS_ANON_EXTENDED_FLOOR-P,2*P));
    assert(!nas_anon_fixed_range_allowed(NAS_ANON_EXTENDED_CEILING,P));
    nas_anon_reset(&high_test);
    nas_anon_reset(&b);
    /* Original Ruby GC has >128 disjoint VMAs while loading its standard
     * library; the registry must grow under real pressure, not return a
     * fabricated userspace ENOMEM once the historical fixed array fills. */
    for (unsigned i=0;i<384U;++i)
        assert(nas_anon_record(&b,NAS_ANON_FLOOR+i*2*P,P));
    assert(b.capacity>=384U);
    struct nas_anon_regions snap={0};
    assert(nas_vma_clone(&snap,&b));
    assert(nas_anon_find_gap(&b,P,&z));
    assert(!nas_anon_forget(&b,NAS_ANON_FLOOR,P/2));
    /* Growth, coalescing and partial unmap remain independent across clones. */
    struct nas_anon_regions coalesced={0};
    assert(nas_vma_clone(&coalesced,&b));
    assert(nas_anon_record(&coalesced,NAS_ANON_FLOOR+P,P));
    assert(nas_anon_covered(&coalesced,NAS_ANON_FLOOR,3*P));
    assert(!nas_anon_record(&b,NAS_ANON_FLOOR+P/2,P));
    for (unsigned i=0;i<snap.capacity;++i)
        assert(b.slots[i].start==snap.slots[i].start && b.slots[i].end==snap.slots[i].end);
    nas_anon_reset(&coalesced);
    nas_anon_reset(&snap);
    nas_anon_reset(&b);
    assert(nas_anon_record(&b,NAS_ANON_FLOOR,3*P));
    for (unsigned i=1;i<384U;++i)
        assert(nas_anon_record(&b,NAS_ANON_FLOOR+(i+1)*4*P,P));
    assert(nas_vma_clone(&snap,&b));
    assert(nas_anon_forget(&b,NAS_ANON_FLOOR+P,P));
    assert(!nas_anon_covered(&b,NAS_ANON_FLOOR,3*P));
    assert(nas_anon_covered(&snap,NAS_ANON_FLOOR,3*P));
    nas_anon_reset(&snap);
    nas_anon_reset(&a);
    assert(nas_anon_find_gap(&a,3*P,&z) && z==x);
    /* A realistic libc allocator reserves 128+ adjacent small chunks:
     * only one VM ownership record must survive coalescing. */
    nas_anon_reset(&a);
    for (unsigned i=0;i<384U;i++) {
        assert(nas_anon_find_gap(&a,P,&x));
        assert(nas_anon_record(&a,x,P));
    }
    assert(nas_anon_covered(&a,x,P));
    assert(nas_anon_covered(&a,NAS_ANON_CEILING-384U*P,
                            384U*P));
    assert(!nas_anon_covered(&a,NAS_ANON_CEILING+P,P));
    assert(!nas_anon_intersects(&a,NAS_ANON_FIXED_FLOOR,P));
    /* MAP_FIXED outside the top-down mmap window is legal for user code;
     * the lower brk/ELF domain can be overlaid without touching the stack. */
    assert(nas_anon_overlay(&a,0x2e6f000,2*P));
    assert(nas_anon_covered(&a,0x2e6f000,2*P));
    assert(nas_anon_overlay(&a,0x2e70000,3*P));
    assert(nas_anon_covered(&a,0x2e6f000,4*P));
    assert(nas_anon_forget(&a,0x2e70000,P));
    assert(!nas_anon_covered(&a,0x2e6f000,4*P));
    assert(nas_anon_overlay(&a,0x2e70000,P));
    assert(nas_anon_covered(&a,0x2e6f000,4*P));
    assert(!nas_anon_overlay(&a,0x3000,P));
    assert(!nas_anon_overlay(&a,NAS_ANON_CEILING-P,2*P));
    /* A split at high VMA density must grow without damaging the owner map. */
    nas_anon_reset(&b);
    assert(nas_anon_record(&b,NAS_ANON_FIXED_FLOOR,3*P));
    for (unsigned i=1;i<384U;i++)
        assert(nas_anon_record(&b,NAS_ANON_FLOOR+4*i*P,P));
    assert(nas_vma_clone(&snap,&b));
    assert(nas_anon_overlay(&b,NAS_ANON_FIXED_FLOOR+4*P,P));
    assert(!nas_vma_find(&snap,NAS_ANON_FIXED_FLOOR+4*P));
    assert(nas_vma_find(&b,NAS_ANON_FIXED_FLOOR+4*P));
    nas_anon_reset(&snap);
    /* M85: logical mmap VMAs are independent of PTEs. PROT_NONE reserves
     * a range even when no physical frame is allocated. */
    nas_anon_reset(&a);
    const uint64_t base=NAS_ANON_FLOOR+0x100000;
    assert(nas_vma_record(&a,base,6*P,0,NAS_VMA_ANON_PRIVATE,0));
    const struct nas_anon_region *v=nas_vma_find(&a,base+2*P);
    assert(v && v->start==base && v->end==base+6*P &&
           v->kind==NAS_VMA_ANON_PRIVATE && v->prot==0);
    assert(nas_vma_protect(&a,base+P,2*P,3));
    v=nas_vma_find(&a,base+P);
    assert(v && v->start==base+P && v->end==base+3*P &&
           v->prot==3);
    assert(nas_vma_find(&a,base)->prot==0);
    assert(nas_vma_find(&a,base+3*P)->prot==0);
    /* M109 Ruby: a demand-zero RW anonymous stack arena is a VMA before
     * its first user touch. Reprotecting an unmaterialized first guard page
     * to NONE must preserve the remaining RW policy and range ownership. */
    nas_anon_reset(&b);
    const uint64_t stack=NAS_ANON_FLOOR+0x200000;
    assert(nas_vma_record(&b,stack,0x1440000,3,NAS_VMA_ANON_PRIVATE,0));
    assert(nas_vma_protect(&b,stack,P,0));
    assert(nas_vma_find(&b,stack)->prot==0);
    assert(nas_vma_find(&b,stack+P)->prot==3);
    assert(nas_anon_covered(&b,stack,0x1440000));
    assert(nas_vma_protect(&b,stack,P,0)); /* Guard reprotection is idempotent. */
    assert(nas_vma_find(&b,stack)->prot==0);
    /* Same kind but differing rights never gets merged. */
    assert(nas_vma_record(&a,base+6*P,P,5,NAS_VMA_ANON_PRIVATE,0));
    assert(nas_vma_find(&a,base+6*P)->prot==5);
    /* MAP_FIXED snapshot overwrites only the selected subinterval. */
    assert(nas_vma_replace(&a,base+2*P,2*P,1,
                           NAS_VMA_FILE_PRIVATE_SNAPSHOT,0x9000));
    v=nas_vma_find(&a,base+2*P);
    assert(v && v->start==base+2*P && v->end==base+4*P &&
           v->prot==1 && v->kind==NAS_VMA_FILE_PRIVATE_SNAPSHOT &&
           v->file_offset==0x9000);
    assert(nas_anon_forget(&a,base+2*P,P));
    v=nas_vma_find(&a,base+3*P);
    assert(v && v->start==base+3*P &&
           v->file_offset==0xa000 && v->end==base+4*P);
    assert(nas_vma_protect(&a,base+3*P,P,3));
    assert(nas_vma_find(&a,base+3*P)->prot==3 &&
           nas_vma_find(&a,base+3*P)->file_offset==0xa000);
    uint64_t cursor=0;
    assert(nas_vma_walk(&a,ordered,&cursor) && cursor>=base+7*P);
    assert(!nas_vma_find(&a,base+2*P));
    /* Reprotecting an unreserved range is not a PTE allocation request. */
    struct nas_anon_regions stable={0};
    assert(nas_vma_clone(&stable,&a));
    assert(!nas_vma_protect(&a,base+2*P,P,1));
    for(unsigned i=0;i<stable.capacity;i++)
        assert(a.slots[i].start==stable.slots[i].start &&
               a.slots[i].end==stable.slots[i].end &&
               a.slots[i].prot==stable.slots[i].prot &&
               a.slots[i].file_offset==stable.slots[i].file_offset);
    nas_anon_reset(&stable);
    /* M85-B: one typed ledger covers mappings outside the allocator
     * window, including the fixed user stack and the ISA trampoline. */
    const uint64_t stack_bottom=NAS_USER_STACK_TOP-NAS_USER_STACK_RESERVE_PAGES*P;
    const uint64_t eager_bottom=NAS_USER_STACK_TOP-NAS_USER_STACK_PAGES*P;
    assert(NAS_USER_STACK_RESERVE_PAGES>NAS_USER_STACK_PAGES);
    assert(eager_bottom>stack_bottom+P);
    assert(NAS_USER_SIGNAL_TRAMPOLINE+P<stack_bottom);
    assert(!nas_anon_fixed_range_allowed(stack_bottom,P));
    assert(!nas_anon_fixed_range_allowed(NAS_USER_SIGNAL_TRAMPOLINE,P));
    assert(nas_vma_record(&a,stack_bottom,
                          NAS_USER_STACK_RESERVE_PAGES*P,3,NAS_VMA_STACK,0));
    assert(nas_vma_record(&a,NAS_USER_SIGNAL_TRAMPOLINE,P,5,
                          NAS_VMA_SIGNAL_TRAMPOLINE,0));
    assert(nas_vma_record(&a,0x10000000,2*P,5,
                          NAS_VMA_ELF_PRIVATE_SNAPSHOT,0x1000));
    assert(nas_vma_find(&a,stack_bottom)->kind==NAS_VMA_STACK);
    assert(nas_vma_find(&a,eager_bottom-P)->kind==NAS_VMA_STACK);
    assert(nas_vma_find(&a,NAS_USER_STACK_TOP-1)->kind==NAS_VMA_STACK);
    assert(!nas_vma_find(&a,stack_bottom-1)); /* Grow-down hard limit. */
    assert(nas_vma_find(&a,NAS_USER_SIGNAL_TRAMPOLINE)->prot==5);
    assert(nas_vma_find(&a,0x10000000)->kind==NAS_VMA_ELF_PRIVATE_SNAPSHOT);
    assert(!nas_anon_overlay(&a,NAS_ANON_CEILING-P,2*P));
    uint64_t top_seen=0;
    assert(nas_vma_walk(&a,ordered,&top_seen) && top_seen==NAS_USER_STACK_TOP);
    nas_anon_reset(&a);
    nas_anon_reset(&b);
    /* A file mapping pins one real backing owner per live fragment. Fork
     * clones and policy rewrites acquire before committing; unmap/exec
     * release exactly the removed fragments, even through OOM rollback. */
    struct nas_mapped_file file={.refs=1}; /* creating FD's temporary owner */
    struct nas_anon_regions owned={0},child={0},failed={0};
    const uint64_t fbase=NAS_ANON_FLOOR+24*P;
    assert(nas_vma_record_shared(&owned,fbase,3*P,3,2*P,&file));
    assert(file.refs==2);
    deny_file_get=true;
    assert(!nas_vma_clone(&failed,&owned));
    assert(!failed.slots && file.refs==2);
    deny_file_get=false;
    assert(nas_vma_clone(&child,&owned) && file.refs==3);
    assert(nas_anon_forget(&owned,fbase+P,P) && file.refs==4);
    const struct nas_anon_region *left=nas_vma_find(&owned,fbase);
    const struct nas_anon_region *right=nas_vma_find(&owned,fbase+2*P);
    assert(left && right && left->backing==&file && right->backing==&file &&
           left->file_offset==2*P && right->file_offset==4*P);
    assert(nas_vma_protect(&owned,fbase+2*P,P,1) && file.refs==4);
    assert(nas_vma_set_dontfork(&owned,fbase+2*P,P,true) && file.refs==4);
    assert((nas_vma_find(&owned,fbase+2*P)->flags&NAS_VMA_DONTFORK)!=0);
    assert(nas_vma_replace(&owned,fbase,P,3,NAS_VMA_ANON_PRIVATE,0));
    assert(file.refs==3 && !nas_vma_find(&owned,fbase)->backing);
    nas_anon_reset(&child);
    nas_anon_reset(&owned);
    assert(file.refs==1);
    return 0;
}
