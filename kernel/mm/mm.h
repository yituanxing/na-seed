#ifndef NASEED_MM_OWNER_H
#define NASEED_MM_OWNER_H
#include <stdint.h>
#include "anon_regions.h"

/* Address space has independent identity and reference lifetime.
 * CLONE_VM/vfork share one MM, fork creates a different MM.  The
 * user virtual layout belongs to this object, never a thread leader.
 * Fork shares resident private frames through refcounted COW leaves. */
struct nas_mm {
    unsigned users;
    uint64_t root_phys;
    uint64_t heap_break;
    struct nas_anon_regions mmap_vmas; /* typed VMAs for mmap-owned ranges */
};

void nas_mm_system_init(void);
struct nas_mm *nas_mm_new(void); /* one owned reference, empty root */
/* Fork shares private data frames read-only, not page tables. Parent PTEs
 * become read-only only after the complete child is built; failure keeps
 * parent register/MM/layout, mappings and refcounts unchanged. */
struct nas_mm *nas_mm_fork_cow(const struct nas_mm *source);
/* Explicit COW break. Ptrace can pass preserve_perm=true to make an RX
 * breakpoint mapping private without upgrading its user permissions.
 * All operations refer to a selected MM, not the current Task's CR3. */
bool nas_mm_break_cow(struct nas_mm *mm,uint64_t va,
                      unsigned logical_prot,bool preserve_perm);
/* Explicit source-MM access independent of executing Task/root register.
 * Return false for absent address space, PTE or invalid userspace VA. */
typedef bool (*nas_mm_page_visit)(uint64_t va,unsigned access,void *ctx);
bool nas_mm_walk_pages(const struct nas_mm *mm,nas_mm_page_visit visit,void *ctx);
bool nas_mm_resolve_user(const struct nas_mm *mm,uint64_t va,
                         uint64_t *physical,unsigned *access);
struct nas_mm *nas_mm_get(struct nas_mm *mm); /* new owned reference */
void nas_mm_put(struct nas_mm *mm); /* final put reclaims page-table root */
#endif
