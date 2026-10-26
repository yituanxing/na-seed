#include <stdint.h>
#include <stdbool.h>
#include "frame.h"
#include "user_space.h"
#include "../arch/mmu.h"
#define PAGE_BYTES 4096ULL
/* The historical eager-copy oracle remains a boot-only page-table test.
 * It is intentionally not linked into the production MM fork API: all
 * running processes fork through nas_mm_fork_cow(). */
struct clone_test_ctx { struct nas_user_space *dest; };
static bool clone_test_leaf(uint64_t va,uint64_t pa,unsigned access,void *opaque) {
    struct clone_test_ctx *ctx=opaque;
    uint64_t copy=nas_frame_alloc();
    if(!copy)return false;
    volatile uint64_t *src=(volatile uint64_t *)(uintptr_t)(nas_hhdm_offset()+pa);
    volatile uint64_t *dst=(volatile uint64_t *)(uintptr_t)(nas_hhdm_offset()+copy);
    for(unsigned i=0;i<PAGE_BYTES/8;i++)dst[i]=src[i];
    if(nas_arch_pt_map(ctx->dest,va,copy,access))return true;
    (void)nas_frame_put(copy);
    return false;
}
static bool clone_test_snapshot(struct nas_user_space *dest,
                                const struct nas_user_space *source) {
    if(!dest || !source || !source->root_pa || dest==source)return false;
    if(!nas_space_new(dest,source->levels))return false;
    struct clone_test_ctx ctx={dest};
    if(nas_arch_pt_walk(source,clone_test_leaf,&ctx))return true;
    (void)nas_space_delete(dest);
    return false;
}
/* Real physical frame allocator and real ISA PTEs exercised by QEMU
 * BEFORE any one-way U-mode transition. Shared implementation and test
 * runs on RV64 and AArch64; no hand-waved off-line mock. */
bool nas_space_regression(void) {
    const uint64_t va = 0x200000ULL;
    uint64_t initial = nas_free_frames();
    /* High canonical user mappings must clone/reclaim too. AArch64 TTBR0
     * uses ALL root entries; RV64 owns lower 256 of SATP root. */
    uint64_t limit=nas_arch_user_limit();
    if(limit<0x400000 || (limit & 4095ULL)) return false;
    const uint64_t high=limit-4096;
    struct nas_user_space a={0}, b={0};
    unsigned levels = nas_arch_pt_active_levels();
    if (!levels || !nas_space_new(&a, levels)) return false;
    if (!nas_space_map_page(&a, va, true) ||
        !nas_space_map_page(&a, high, true)) return false;
    /* Exact user permissions rather than legacy writable->executable.
     * Exercise both native RV64 SvX and ARM64 TTBR0 PTE encoders. */
    const uint64_t ro=0x240000ULL, rx=0x250000ULL;
    if (!nas_space_map_access(&a, ro, NAS_UP_READ) ||
        !nas_space_map_access(&a, rx, NAS_UP_READ|NAS_UP_EXEC)) return false;
    unsigned ro_access=0, rx_access=0;
    if (!nas_space_lookup(&a, ro, 0, &ro_access) ||
        !nas_space_lookup(&a, rx, 0, &rx_access) ||
        ro_access != NAS_UP_READ ||
        rx_access != (NAS_UP_READ|NAS_UP_EXEC)) return false;
    if (nas_space_protect(&a, ro, 1, NAS_UP_WRITE|NAS_UP_EXEC) ||
        !nas_space_lookup(&a, ro, 0, &ro_access) ||
        ro_access != NAS_UP_READ) return false;
    uint8_t *high_a=nas_space_user_byte(&a,high+71);
    if(!high_a) return false;
    *high_a=0x5a;
    uint64_t pa_a=0, pa_b=0;
    unsigned rights=0;
    if (!nas_space_lookup(&a, va+19, &pa_a, &rights) ||
        !(rights & NAS_UP_WRITE)) return false;
    uint8_t *first = nas_space_user_byte(&a, va+19);
    if (!first) return false;
    *first=0x47;
    if (!clone_test_snapshot(&b,&a)) return false;
    uint8_t *high_b=nas_space_user_byte(&b,high+71);
    if(!high_b || *high_b!=0x5a || high_b==high_a) return false;
    *high_b=0x33;
    if(*high_a!=0x5a) return false;
    if (!nas_space_lookup(&b, va+19, &pa_b, &rights) ||
        pa_b==pa_a || !nas_space_user_byte(&b, va+19)) return false;
    if (*nas_space_user_byte(&b,va+19)!=0x47) return false;
    *nas_space_user_byte(&b,va+19)=0x92;
    if (*first!=0x47) return false;
    if (!nas_space_protect(&a,va,1,NAS_UP_READ|NAS_UP_EXEC) ||
        nas_space_user_writable(&a,va)) return false;
    if (!nas_space_unmap_page(&a,va) || nas_space_user_byte(&a,va))
        return false;
    if (!nas_space_delete(&a) || !nas_space_delete(&b)) return false;
    return nas_free_frames()==initial;
}
