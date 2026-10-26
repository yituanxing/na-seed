#ifndef NASEED_ANON_REGIONS_H
#define NASEED_ANON_REGIONS_H
#include <stdbool.h>
#include <stdint.h>
#define NAS_ANON_REGION_LIMIT 128U
#define NAS_ANON_FIXED_FLOOR 0x00010000ULL
#define NAS_ANON_FLOOR 0x20000000ULL
#define NAS_ANON_CEILING 0x60000000ULL
/* Policy records for mmap-owned private user VMAs, independent of resident
 * pages and PTE permissions. PROT_NONE is a VMA even with no resident PTE.
 * File snapshots are eagerly private; no durable file backing yet.
 * ELF/brk/stack still require integration before this is a complete VMA map. */
enum nas_vma_kind { NAS_VMA_ANON_PRIVATE=0, NAS_VMA_FILE_PRIVATE_SNAPSHOT=1 };
struct nas_anon_region {
    uint64_t start,end;
    uint64_t file_offset; /* meaningful for eagerly copied file mappings */
    uint32_t prot;        /* logical permissions; NOT hardware PTE state */
    uint32_t kind;
};
struct nas_anon_regions { struct nas_anon_region slots[NAS_ANON_REGION_LIMIT]; };
typedef bool (*nas_vma_visit)(const struct nas_anon_region *,void *);
const struct nas_anon_region *nas_vma_find(const struct nas_anon_regions *,uint64_t va);
bool nas_vma_walk(const struct nas_anon_regions *,nas_vma_visit,void *);
bool nas_vma_record(struct nas_anon_regions *,uint64_t addr,uint64_t length,
                    unsigned prot,unsigned kind,uint64_t file_offset);
bool nas_vma_replace(struct nas_anon_regions *,uint64_t addr,uint64_t length,
                     unsigned prot,unsigned kind,uint64_t file_offset);
bool nas_vma_protect(struct nas_anon_regions *,uint64_t addr,uint64_t length,
                     unsigned prot);
/* M11 reservation APIs are retained as compatibility wrappers. These and
 * the typed VMA routines manipulate the SAME canonical MM-owned slots. */
void nas_anon_reset(struct nas_anon_regions *);
bool nas_anon_find_gap(const struct nas_anon_regions *,uint64_t length,uint64_t *);
bool nas_anon_record(struct nas_anon_regions *,uint64_t addr,uint64_t length);
bool nas_anon_forget(struct nas_anon_regions *,uint64_t addr,uint64_t length);
bool nas_anon_covered(const struct nas_anon_regions *,uint64_t addr,uint64_t length);
bool nas_anon_intersects(const struct nas_anon_regions *,uint64_t addr,uint64_t length);
bool nas_anon_overlay(struct nas_anon_regions *,uint64_t addr,uint64_t length);
#endif
