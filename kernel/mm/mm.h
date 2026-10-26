#ifndef NASEED_MM_OWNER_H
#define NASEED_MM_OWNER_H
#include <stdint.h>
#include "anon_regions.h"

/* Address space has independent identity and reference lifetime.
 * CLONE_VM/vfork share one MM, fork creates a different MM.  The
 * user virtual layout belongs to this object, never a thread leader.
 * Physical user frames remain eagerly private until page refcounts/COW. */
struct nas_mm {
    unsigned users;
    uint64_t root_phys;
    uint64_t heap_break;
    struct nas_anon_regions anonymous;
};

void nas_mm_system_init(void);
struct nas_mm *nas_mm_new(void); /* one owned reference, empty root */
/* Duplicate current executing MM with independent private physical pages.
 * Baseline eager-fork implementation: COW can later replace just this policy. */
struct nas_mm *nas_mm_fork_eager(const struct nas_mm *source);
/* Explicit source-MM access independent of executing Task/root register.
 * Return false for absent address space, PTE or invalid userspace VA. */
typedef bool (*nas_mm_page_visit)(uint64_t va,unsigned access,void *ctx);
bool nas_mm_walk_pages(const struct nas_mm *mm,nas_mm_page_visit visit,void *ctx);
bool nas_mm_resolve_user(const struct nas_mm *mm,uint64_t va,
                         uint64_t *physical,unsigned *access);
struct nas_mm *nas_mm_get(struct nas_mm *mm); /* new owned reference */
void nas_mm_put(struct nas_mm *mm); /* final put reclaims page-table root */
#endif
