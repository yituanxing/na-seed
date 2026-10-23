#ifndef NASEED_ANON_REGIONS_H
#define NASEED_ANON_REGIONS_H
#include <stdbool.h>
#include <stdint.h>
#define NAS_ANON_REGION_LIMIT 128U
#define NAS_ANON_FIXED_FLOOR 0x00010000ULL
#define NAS_ANON_FLOOR 0x20000000ULL
#define NAS_ANON_CEILING 0x60000000ULL
/* Per-process ownership ledger of PRIVATE user mappings. Fixed remaps may
 * occur below the top-down mmap allocation window, e.g. musl/cc1 brk guards.
 * Adjacent ranges coalesce: repeated small allocations do not consume a
 * metadata slot each. Frames/PTEs remain owned by the native MMU provider. */
struct nas_anon_region { uint64_t start,end; };
struct nas_anon_regions { struct nas_anon_region slots[NAS_ANON_REGION_LIMIT]; };
void nas_anon_reset(struct nas_anon_regions *regions);
bool nas_anon_find_gap(const struct nas_anon_regions *regions,
                       uint64_t length,uint64_t *addr);
bool nas_anon_record(struct nas_anon_regions *regions,
                     uint64_t addr,uint64_t length);
bool nas_anon_forget(struct nas_anon_regions *regions,
                     uint64_t addr,uint64_t length);
bool nas_anon_covered(const struct nas_anon_regions *regions,
                      uint64_t addr,uint64_t length);
bool nas_anon_intersects(const struct nas_anon_regions *regions,
                         uint64_t addr,uint64_t length);
/* Prepare a MAP_FIXED reservation entirely in a snapshot: overlay is
 * committed only if the resulting metadata is representable. No PTE ops. */
bool nas_anon_overlay(struct nas_anon_regions *regions,
                      uint64_t addr,uint64_t length);
#endif
