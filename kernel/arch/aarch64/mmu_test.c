#include <stdbool.h>
#include <stdint.h>
#include "../../mm/frame.h"
#include "mmu.h"

/* Match the *semantic* assertions of the real RISC-V offline PTE tests
 * while allowing a completely different ARM descriptor implementation. */
bool nas_arm_pt_smoke(void) {
    for (unsigned levels = 3; levels <= 4; levels++) {
        uint64_t initial = nas_free_frames();
        struct nas_arm_pt p = {0};
        if (!nas_arm_pt_init(&p, levels)) return false;
        uint64_t data = nas_frame_alloc(), code = nas_frame_alloc();
        if (!data || !code || data == code) return false;
        const uint64_t va1 = 0x1ff000, va2 = 0x200000;
        const uint64_t user_data = NAS_ARM_R | NAS_ARM_W | NAS_ARM_USER;
        const uint64_t user_code = NAS_ARM_R | NAS_ARM_X | NAS_ARM_USER;
        if (!nas_arm_pt_map(&p, va1, data, user_data) ||
            !nas_arm_pt_map(&p, va2, code, user_code)) return false;
        uint64_t got = 0, flags = 0;
        if (!nas_arm_pt_translate(&p, va1 + 0x123, &got, &flags) ||
            got != data + 0x123 || flags != user_data) return false;
        if (!nas_arm_pt_translate(&p, va2 + 0x456, &got, &flags) ||
            got != code + 0x456 || flags != user_code) return false;
        uint64_t transient=nas_frame_alloc(), removed=0;
        const uint64_t candidate=0x400000ULL;
        if(!transient || !nas_arm_pt_prepare(&p,candidate) ||
           !nas_arm_pt_exchange(&p,candidate,transient,
                                NAS_UP_READ|NAS_UP_WRITE,&removed) ||
           removed || !nas_arm_pt_translate(&p,candidate,&got,0) ||
           got!=transient ||
           !nas_arm_pt_exchange(&p,candidate,0,0,&removed) ||
           removed!=transient ||
           nas_arm_pt_translate(&p,candidate,&got,0) ||
           !nas_frame_put(removed))return false;
        if (nas_arm_pt_map(&p, va1, code, user_data) ||
            nas_arm_pt_map(&p, va1 + 1, code, user_data) ||
            nas_arm_pt_map(&p, va2 + NAS_ARM_PAGE_SIZE, code,
                           NAS_ARM_USER | NAS_ARM_W) ||
            nas_arm_pt_map(&p, nas_arm_pt_user_limit(levels), data,
                           user_data)) return false;
        const uint64_t high = nas_arm_pt_user_limit(levels) - NAS_ARM_PAGE_SIZE;
        if (!nas_arm_pt_map(&p, high, data, user_code) ||
            !nas_arm_pt_translate(&p, high + 0xfff, &got, &flags) ||
            got != data + 0xfff || flags != user_code ||
            !nas_arm_pt_unmap(&p, high) ||
            !nas_arm_pt_unmap(&p, va1) ||
            nas_arm_pt_translate(&p, va1, &got, 0) ||
            nas_arm_pt_unmap(&p, va1)) return false;
        nas_arm_pt_destroy(&p);
        if (p.root_pa || p.levels || !nas_frame_put(data) ||
            !nas_frame_put(code) || nas_free_frames() != initial)
            return false;
    }
    return true;
}
