#ifndef NASEED_ANON_REGIONS_H
#define NASEED_ANON_REGIONS_H
#include <stdbool.h>
#include <stdint.h>
#define NAS_ANON_REGION_LIMIT 64U
#define NAS_ANON_FLOOR 0x20000000ULL
#define NAS_ANON_CEILING 0x60000000ULL
/* Eager anonymous VMA ranges are per-process and copied by value on fork.
 * PTE ownership and frame reclamation stay in the existing MMU providers. */
struct nas_anon_region { uint64_t start,end; };
struct nas_anon_regions { struct nas_anon_region slots[NAS_ANON_REGION_LIMIT]; };
void nas_anon_reset(struct nas_anon_regions *regions);
bool nas_anon_find_gap(const struct nas_anon_regions *regions,
                       uint64_t length,uint64_t *addr);
bool nas_anon_record(struct nas_anon_regions *regions,
                     uint64_t addr,uint64_t length);
bool nas_anon_forget(struct nas_anon_regions *regions,
                     uint64_t addr,uint64_t length);
#endif
