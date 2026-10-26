#ifndef NASEED_RISCV64_MMU_H
#define NASEED_RISCV64_MMU_H
#include <stdbool.h>
#include <stdint.h>
#include "../user_pt.h"

/* A first genuine RV64 page-table backend. The caller owns mapped data
 * frames. init/destroy allocate and reclaim page-table frames only.
 * Page size is 4KiB; root mode selects Sv39/Sv48/Sv57 (3/4/5 levels).
 * AArch64/x86 page-table formats must NOT be forced into this structure. */
#define NAS_RV_PAGE_SIZE 4096ULL
enum {
    NAS_RV_PTE_R = 1u << 1,
    NAS_RV_PTE_W = 1u << 2,
    NAS_RV_PTE_X = 1u << 3,
    NAS_RV_PTE_U = 1u << 4,
};
struct nas_rv_page_table {
    uint64_t root_pa;
    unsigned levels;
};
/* Page-table API constructs mappings offline; RV64 usermode smoke is a
 * separate one-way architectural SATP switch test. */
bool nas_rv_pt_init(struct nas_rv_page_table *pt, unsigned levels);
void nas_rv_pt_destroy(struct nas_rv_page_table *pt);
bool nas_rv_pt_prepare(const struct nas_rv_page_table *,uint64_t va);
bool nas_rv_pt_exchange(const struct nas_rv_page_table *,
                       uint64_t va,uint64_t new_pa,unsigned access,uint64_t *old_pa);
bool nas_rv_pt_map(struct nas_rv_page_table *pt, uint64_t user_va,
                   uint64_t pa, uint64_t perms);
bool nas_rv_pt_translate(const struct nas_rv_page_table *pt, uint64_t user_va,
                         uint64_t *pa, uint64_t *perms);
bool nas_rv_pt_unmap(struct nas_rv_page_table *pt, uint64_t user_va);
uint64_t nas_rv_pt_user_limit(unsigned levels);
bool nas_rv_pt_smoke(void);
/* Genuine native 4K PTE permission edits and lower-half user-only walk. */
bool nas_rv_pt_protect(const struct nas_rv_page_table *, uint64_t va,
                       unsigned access);
bool nas_rv_pt_walk(const struct nas_rv_page_table *, nas_user_pt_visit, void *);
#endif
