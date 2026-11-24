/* Real architecture PTE implementations and smoke tests. Mock ONLY the
 * physical allocator, permitting deterministic directory allocation failures. */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/arch/aarch64/mmu.h"
#include "../kernel/arch/riscv64/mmu.h"
#include "../kernel/mm/frame.h"
#define P 4096ULL
#define N 128U
static uint64_t frames[N][512];
static bool used[N];
static unsigned alloc_count, fail_after;
uint64_t nas_hhdm_offset(void) { return (uintptr_t)frames; }
uint64_t nas_free_frames(void) {
    uint64_t n=0;
    for(unsigned i=1;i<N;i++) if(!used[i])n++;
    return n;
}
uint64_t nas_frame_alloc(void) {
    if(fail_after && ++alloc_count==fail_after)return 0;
    for(unsigned i=1;i<N;i++)if(!used[i]){
        used[i]=true;
        memset(frames[i],0,sizeof(frames[i]));
        return i*P;
    }
    return 0;
}
bool nas_frame_put(uint64_t pa) {
    assert(pa && !(pa&(P-1)) && pa/P<N && used[pa/P]);
    used[pa/P]=false;
    memset(frames[pa/P],0xa5,sizeof(frames[0]));
    return true;
}
struct walk_result { unsigned count; uint64_t va,pa; unsigned access; };
static bool visit(uint64_t va,uint64_t pa,unsigned access,void *ctx) {
    struct walk_result *r=ctx;
    r->count++;r->va=va;r->pa=pa;r->access=access;
    return true;
}
/* Compile both real backends and execute all five supported level variants.
 * The fixture uses typed interfaces; no incompatible struct aliasing. */
#define EXERCISE(prefix,type,levels_max,flags) do { \
    for(unsigned levels=3;levels<=levels_max;levels++) { \
        const uint64_t initial=nas_free_frames(); \
        struct type pt={0}; \
        assert(prefix##_pt_init(&pt,levels)); \
        uint64_t pa=nas_frame_alloc(), other=nas_frame_alloc(); \
        assert(pa && other); \
        const uint64_t before=nas_free_frames(), va=0x40000000ULL; \
        for(unsigned n=1;n<levels;n++) { \
            alloc_count=0;fail_after=n; \
            assert(!prefix##_pt_prepare(&pt,va)); \
            fail_after=0;assert(nas_free_frames()==before); \
            alloc_count=0;fail_after=n; \
            assert(!prefix##_pt_map(&pt,va,pa,flags)); \
            fail_after=0;assert(nas_free_frames()==before); \
        } \
        assert(prefix##_pt_map(&pt,va,pa,flags)); \
        assert(nas_free_frames()==before-(levels-1)); \
        assert(!prefix##_pt_map(&pt,va,other,flags)); \
        uint64_t got=0; \
        assert(prefix##_pt_translate(&pt,va+17,&got,0) && got==pa+17); \
        assert(prefix##_pt_protect(&pt,va,NAS_UP_READ|NAS_UP_EXEC)); \
        struct walk_result r={0}; \
        assert(prefix##_pt_walk(&pt,visit,&r) && r.count==1 && \
               r.va==va && r.pa==pa && r.access==(NAS_UP_READ|NAS_UP_EXEC)); \
        assert(prefix##_pt_map(&pt,va+P,other,flags)); \
        assert(prefix##_pt_unmap(&pt,va)); \
        assert(nas_free_frames()==before-(levels-1)); \
        assert(prefix##_pt_unmap(&pt,va+P)); \
        assert(nas_free_frames()==before); \
        assert(!prefix##_pt_unmap(&pt,va)); \
        assert(prefix##_pt_prepare(&pt,va)); \
        assert(prefix##_pt_exchange(&pt,va,pa,NAS_UP_READ,&got) && got==0); \
        assert(prefix##_pt_exchange(&pt,va,0,0,&got) && got==pa); \
        prefix##_pt_destroy(&pt); \
        assert(!pt.root_pa && !pt.levels); \
        assert(nas_frame_put(pa) && nas_frame_put(other)); \
        assert(nas_free_frames()==initial); \
    } \
} while(0)
int main(void) {
    assert(nas_arm_pt_smoke());
    assert(nas_rv_pt_smoke());
    EXERCISE(nas_arm,nas_arm_pt,4,NAS_ARM_USER|NAS_ARM_R|NAS_ARM_W);
    EXERCISE(nas_rv,nas_rv_page_table,5,NAS_RV_PTE_U|NAS_RV_PTE_R|NAS_RV_PTE_W);
    puts("[PT DIRECTORY PASS] five paging depths, OOM rollback, map/prepare/exchange, protect/walk, sibling reclaim, data ownership");
    return 0;
}
