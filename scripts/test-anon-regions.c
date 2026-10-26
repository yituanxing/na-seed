#include <assert.h>
#include <stdint.h>
#include "../kernel/mm/anon_regions.h"
#define P 4096ULL
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
    b=a;
    assert(nas_anon_forget(&b,x,3*P));
    assert(nas_anon_find_gap(&b,3*P,&z) && z==x);
    assert(nas_anon_find_gap(&a,P,&z) && z==y-P);
    assert(nas_anon_forget(&a,0x10000000,P));
    assert(nas_anon_find_gap(&a,P,&z) && z==y-P);
    assert(!nas_anon_record(&a,x,3*P));
    assert(!nas_anon_record(&a,x+1,P));
    assert(!nas_anon_find_gap(&a,UINT64_MAX,&z));
    assert(!nas_anon_forget(&a,UINT64_MAX-1023,2*P));
    nas_anon_reset(&b);
    for (unsigned i=0;i<NAS_ANON_REGION_LIMIT;++i)
        assert(nas_anon_record(&b,NAS_ANON_FLOOR+i*2*P,P));
    struct nas_anon_regions snap=b;
    assert(!nas_anon_find_gap(&b,P,&z));
    assert(!nas_anon_forget(&b,NAS_ANON_FLOOR,P/2));
    /* Full registry can merge its adjacent gap without a new slot. */
    struct nas_anon_regions coalesced=b;
    assert(nas_anon_record(&coalesced,NAS_ANON_FLOOR+P,P));
    assert(nas_anon_covered(&coalesced,NAS_ANON_FLOOR,3*P));
    assert(!nas_anon_record(&b,NAS_ANON_FLOOR+P/2,P));
    for (unsigned i=0;i<NAS_ANON_REGION_LIMIT;++i)
        assert(b.slots[i].start==snap.slots[i].start && b.slots[i].end==snap.slots[i].end);
    nas_anon_reset(&b);
    assert(nas_anon_record(&b,NAS_ANON_FLOOR,3*P));
    for (unsigned i=1;i<NAS_ANON_REGION_LIMIT;++i)
        assert(nas_anon_record(&b,NAS_ANON_FLOOR+(i+1)*4*P,P));
    snap=b;
    assert(!nas_anon_forget(&b,NAS_ANON_FLOOR+P,P));
    for (unsigned i=0;i<NAS_ANON_REGION_LIMIT;++i)
        assert(b.slots[i].start==snap.slots[i].start && b.slots[i].end==snap.slots[i].end);
    nas_anon_reset(&a);
    assert(nas_anon_find_gap(&a,3*P,&z) && z==x);
    /* A realistic libc allocator reserves 128+ adjacent small chunks:
     * only one VM ownership record must survive coalescing. */
    nas_anon_reset(&a);
    for (unsigned i=0;i<NAS_ANON_REGION_LIMIT*2;i++) {
        assert(nas_anon_find_gap(&a,P,&x));
        assert(nas_anon_record(&a,x,P));
    }
    assert(nas_anon_covered(&a,x,P));
    assert(nas_anon_covered(&a,NAS_ANON_CEILING-NAS_ANON_REGION_LIMIT*2*P,
                            NAS_ANON_REGION_LIMIT*2*P));
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
    /* Split budget failure must leave the owner map unchanged. */
    nas_anon_reset(&b);
    assert(nas_anon_record(&b,NAS_ANON_FIXED_FLOOR,3*P));
    for (unsigned i=1;i<NAS_ANON_REGION_LIMIT;i++)
        assert(nas_anon_record(&b,NAS_ANON_FLOOR+4*i*P,P));
    snap=b;
    assert(!nas_anon_overlay(&b,NAS_ANON_FIXED_FLOOR+4*P,P));
    for (unsigned i=0;i<NAS_ANON_REGION_LIMIT;i++)
        assert(b.slots[i].start==snap.slots[i].start &&
               b.slots[i].end==snap.slots[i].end);
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
    struct nas_anon_regions stable=a;
    assert(!nas_vma_protect(&a,base+2*P,P,1));
    for(unsigned i=0;i<NAS_ANON_REGION_LIMIT;i++)
        assert(a.slots[i].start==stable.slots[i].start &&
               a.slots[i].end==stable.slots[i].end &&
               a.slots[i].prot==stable.slots[i].prot &&
               a.slots[i].file_offset==stable.slots[i].file_offset);
    return 0;
}
