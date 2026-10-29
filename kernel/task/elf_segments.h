#ifndef NASEED_ELF_SEGMENTS_H
#define NASEED_ELF_SEGMENTS_H
#include <stdint.h>
#include <stdbool.h>
#include "elf.h"
/* One program-header validator AND PT_LOAD execution algorithm shared
 * between full-kernel execve and native RV64 user-mode boot.
 * The target supplies only address-space map/copy capabilities.
 */
struct nas_elf_segment_ops {
    void *ctx;
    bool (*map_page)(void *ctx, uint64_t va, unsigned elf_flags);
    bool (*copy_bytes)(void *ctx, uint64_t va, const uint8_t *src,
                       uint64_t count);
    /* Optional logical VMA registration: one callback per constant-permission
     * interval, never per 4KiB page (the kernel has a bounded VMA ledger).
     * ELF flags are the page-wise union of every overlapping PT_LOAD. */
    bool (*record_region)(void *ctx, uint64_t start, uint64_t length,
                          unsigned elf_flags, uint64_t file_offset);
};
struct nas_elf_segment_policy {
    uint16_t machine;
    bool allow_dynamic_elf;
    bool reject_pt_dynamic;
    bool reject_overlapping_pages;
    bool require_page_congruence;
    bool forbid_wx;
    unsigned max_phnum;
    uint64_t bias, min_va, max_va; /* [min_va, max_va) includes PT_LOAD range */
    uint64_t max_segment, max_pages; /* max_pages == 0 means uncapped */
    const char *interpreter;         /* NULL rejects PT_INTERP */
};
struct nas_elf_segment_result {
    uint64_t entry, phdr, end;
    uint16_t phnum;
    bool has_interp;
};
bool nas_elf_segments_load(const struct elf_image *image,
    const struct nas_elf_segment_policy *policy,
    const struct nas_elf_segment_ops *ops,
    struct nas_elf_segment_result *result);
#endif
