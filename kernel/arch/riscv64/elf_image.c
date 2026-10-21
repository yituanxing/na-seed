#include <stdint.h>
#include <stdbool.h>
#include "../../task/elf_segments.h"
#include "../../mm/user_space.h"
#include "../abi.h"
#include "elf_image.h"
/* Only the native user VM provider is architecture-specific. ELF parsing,
 * PT_LOAD validation, copy, BSS and entry checks are shared with execve. */
static bool rv_map(void *ctx,uint64_t va,unsigned flags) {
    struct nas_user_space *space=(struct nas_user_space *)ctx;
    unsigned access=NAS_UP_READ |
        ((flags & 2U)?NAS_UP_WRITE:0) |
        ((flags & 1U)?NAS_UP_EXEC:0);
    return nas_space_map_access(space,va,access);
}
static bool rv_copy(void *ctx,uint64_t va,const uint8_t *data,uint64_t len) {
    struct nas_user_space *space=(struct nas_user_space *)ctx;
    for(uint64_t i=0;i<len;i++) {
        uint8_t *out=nas_space_user_byte(space,va+i);
        if(!out) return false;
        *out=data[i];
    }
    return true;
}
bool nas_rv_elf_load(struct nas_user_space *space,
                     const uint8_t *image,uint64_t size,uint64_t *entry) {
    if(!space || !space->root_pa || !image || !entry ||
       size>(1ULL<<20)) return false;
    const struct elf_image elf={image,size};
    const struct nas_elf_segment_policy policy={
        .machine=NAS_ELF_MACHINE,
        .allow_dynamic_elf=false,
        .reject_pt_dynamic=true,
        .reject_overlapping_pages=true,
        .require_page_congruence=true,
        .forbid_wx=true,
        .max_phnum=16,
        .bias=0,
        .min_va=0x10000ULL,
        .max_va=0x20000ULL,
        .max_segment=0x10000ULL,
        .max_pages=32,
        .interpreter=0
    };
    const struct nas_elf_segment_ops callbacks={
        .ctx=space,.map_page=rv_map,.copy_bytes=rv_copy
    };
    struct nas_elf_segment_result result={0};
    if(!nas_elf_segments_load(&elf,&policy,&callbacks,&result)) return false;
    *entry=result.entry;
    return true;
}
