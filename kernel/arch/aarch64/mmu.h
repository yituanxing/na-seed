#ifndef NASEED_AARCH64_MMU_H
#define NASEED_AARCH64_MMU_H
#include <stdint.h>
#include "../user_pt.h"
#include <stdbool.h>

/* Offline TTBR0 user mappings, stage-1 4KiB granule, 39/48 bit VAs.
 * No live TTBR0/switch or TLB manipulation. Data frames caller-owned.
 * ARM descriptors are architecture-owned, never generic RISC-V flags. */
#define NAS_ARM_PAGE_SIZE 4096ULL
enum nas_arm_access {
    NAS_ARM_R = 1u << 0,
    NAS_ARM_W = 1u << 1,
    NAS_ARM_X = 1u << 2,
    NAS_ARM_USER = 1u << 3
};
struct nas_arm_pt {
    uint64_t root_pa;
    unsigned levels; /* 3 for 39-bit, 4 for 48-bit */
};
uint64_t nas_arm_pt_user_limit(unsigned levels);
bool nas_arm_pt_init(struct nas_arm_pt *, unsigned levels);
bool nas_arm_pt_map(struct nas_arm_pt *, uint64_t va,
                    uint64_t pa, uint64_t access);
bool nas_arm_pt_translate(const struct nas_arm_pt *, uint64_t va,
                          uint64_t *pa, uint64_t *access);
bool nas_arm_pt_unmap(struct nas_arm_pt *, uint64_t va);
void nas_arm_pt_destroy(struct nas_arm_pt *);
bool nas_arm_pt_smoke(void);
/* Genuine EL0 PTE permission edits and lower-half user-only walk. */
bool nas_arm_pt_protect(const struct nas_arm_pt *, uint64_t va,
                        unsigned access);
bool nas_arm_pt_walk(const struct nas_arm_pt *, nas_user_pt_visit, void *);
#endif
