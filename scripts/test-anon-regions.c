#include <assert.h>
#include <stdint.h>
#include "../kernel/mm/anon_regions.h"
#define P 4096ULL
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
    assert(!nas_anon_record(&b,NAS_ANON_FLOOR+P,P));
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
    return 0;
}
