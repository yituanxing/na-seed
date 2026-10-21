#include <stdint.h>
#include <stdbool.h>
#include "frame.h"
#include "user_space.h"
#include "../arch/mmu.h"
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
    if (!nas_space_clone(&b,&a)) return false;
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
