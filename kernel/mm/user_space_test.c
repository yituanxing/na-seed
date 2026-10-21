#include <stdint.h>
#include <stdbool.h>
#include "frame.h"
#include "user_space.h"
/* Real physical frame allocator and real ISA PTEs exercised by QEMU
 * BEFORE any one-way U-mode transition. Shared implementation and test
 * runs on RV64 and AArch64; no hand-waved off-line mock. */
bool nas_space_regression(void) {
    const uint64_t va = 0x200000ULL;
    uint64_t initial = nas_free_frames();
    struct nas_user_space a={0}, b={0};
    unsigned levels = nas_arch_pt_active_levels();
    if (!levels || !nas_space_new(&a, levels)) return false;
    if (!nas_space_map_page(&a, va, true)) return false;
    uint64_t pa_a=0, pa_b=0;
    unsigned rights=0;
    if (!nas_space_lookup(&a, va+19, &pa_a, &rights) ||
        !(rights & NAS_UP_WRITE)) return false;
    uint8_t *first = nas_space_user_byte(&a, va+19);
    if (!first) return false;
    *first=0x47;
    if (!nas_space_clone(&b,&a)) return false;
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
