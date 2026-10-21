#include <stdbool.h>
#include <stdint.h>
#include "../../mm/frame.h"
#include "mmu.h"

/* The virtual pages in these tests are NOT installed in satp. Each test
 * creates genuine PTE tables backed by the real Limine frame allocator.
 * Mapping/query/unmapping is checked for every supported paging depth. */
bool nas_rv_pt_smoke(void) {
    const uint64_t first = 0x001ff000ULL;
    const uint64_t second = 0x00200000ULL; /* crosses a 2MiB table boundary */
    for (unsigned levels = 3; levels <= 5; levels++) {
        uint64_t initial = nas_free_frames();
        struct nas_rv_page_table pt = {0};
        if (!nas_rv_pt_init(&pt, levels)) return false;
        uint64_t data_a = nas_frame_alloc(), data_b = nas_frame_alloc();
        if (!data_a || !data_b || data_a == data_b) return false;
        if (!nas_rv_pt_map(&pt, first, data_a, NAS_RV_PTE_R |
                           NAS_RV_PTE_W | NAS_RV_PTE_U)) return false;
        if (!nas_rv_pt_map(&pt, second, data_b, NAS_RV_PTE_R |
                           NAS_RV_PTE_X | NAS_RV_PTE_U)) return false;
        uint64_t pa = 0, perms = 0;
        if (!nas_rv_pt_translate(&pt, first + 0x123, &pa, &perms) ||
            pa != data_a + 0x123 || !(perms & NAS_RV_PTE_W)) return false;
        if (!nas_rv_pt_translate(&pt, second + 0x456, &pa, &perms) ||
            pa != data_b + 0x456 || (perms & NAS_RV_PTE_W) ||
            !(perms & NAS_RV_PTE_X)) return false;
        if (nas_rv_pt_map(&pt, first, data_b, NAS_RV_PTE_R | NAS_RV_PTE_U))
            return false; /* no destructive remap */
        if (nas_rv_pt_map(&pt, nas_rv_pt_user_limit(levels), data_a,
                          NAS_RV_PTE_R | NAS_RV_PTE_U)) return false;
        if (nas_rv_pt_map(&pt, first + 1, data_a,
                          NAS_RV_PTE_R | NAS_RV_PTE_U)) return false;
        if (nas_rv_pt_map(&pt, second + NAS_RV_PAGE_SIZE, data_b,
                          NAS_RV_PTE_W | NAS_RV_PTE_U)) return false;
        /* Exercise top valid userspace directory, including high indices. */
        uint64_t top = nas_rv_pt_user_limit(levels) - NAS_RV_PAGE_SIZE;
        if (!nas_rv_pt_map(&pt, top, data_a,
                           NAS_RV_PTE_R | NAS_RV_PTE_U)) return false;
        if (!nas_rv_pt_translate(&pt, top + 0xfff, &pa, 0) ||
            pa != data_a + 0xfff) return false;
        if (!nas_rv_pt_unmap(&pt, top)) return false;
        if (!nas_rv_pt_unmap(&pt, first)) return false;
        if (nas_rv_pt_translate(&pt, first, &pa, 0)) return false;
        if (nas_rv_pt_unmap(&pt, first)) return false;
        /* Release second PTE via destroy: caller still owns the data frames. */
        nas_rv_pt_destroy(&pt);
        if (pt.root_pa || pt.levels) return false;
        if (!nas_frame_free(data_a) || !nas_frame_free(data_b) ||
            nas_free_frames() != initial) return false;
    }
    return true;
}
