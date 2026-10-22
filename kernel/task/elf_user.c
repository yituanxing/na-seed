#include <stdint.h>
#include <stdbool.h>
#include "elf_user.h"
#include "elf_segments.h"
#include "../arch/abi.h"
#include "../mm/user_space.h"
/* The exact same PT_LOAD/BSS engine handles all architectures. This adapter
 * only selects the VM backend, never contains per-ISA ELF header logic. */
static bool map_page(void *opaque,uint64_t va,unsigned flags) {
    struct nas_user_space *space=(struct nas_user_space *)opaque;
    unsigned access=NAS_UP_READ |
        ((flags & 2U)?NAS_UP_WRITE:0) |
        ((flags & 1U)?NAS_UP_EXEC:0);
    return nas_space_map_access(space,va,access);
}
static bool copy_bytes(void *opaque,uint64_t va,
                       const uint8_t *data,uint64_t n) {
    struct nas_user_space *space=(struct nas_user_space *)opaque;
    for(uint64_t i=0;i<n;i++) {
        uint8_t *dst=nas_space_user_byte(space,va+i);
        if(!dst) return false;
        *dst=data[i];
    }
    return true;
}
bool nas_user_elf_load(struct nas_user_space *space,
                       const uint8_t *image,uint64_t bytes,
                       const struct nas_user_elf_limits *limits,
                       uint64_t *entry) {
    if(!space || !space->root_pa || !limits || !image || !entry ||
       !limits->max_phnum || !limits->max_image_bytes ||
       bytes>limits->max_image_bytes ||
       limits->min_va>=limits->max_va)
        return false;
    const struct elf_image elf={image,bytes};
    const struct nas_elf_segment_policy policy={
        .machine=NAS_ELF_MACHINE,
        .allow_dynamic_elf=limits->allow_dynamic,
        .reject_pt_dynamic=!limits->allow_dynamic,
        .reject_overlapping_pages=!limits->allow_dynamic,
        .require_page_congruence=true,
        .forbid_wx=true,
        .max_phnum=limits->max_phnum,
        .bias=0,
        .min_va=limits->min_va,
        .max_va=limits->max_va,
        .max_segment=limits->max_va-limits->min_va,
        .max_pages=limits->max_pages,
        .interpreter=0
    };
    const struct nas_elf_segment_ops ops={
        .ctx=space,.map_page=map_page,.copy_bytes=copy_bytes
    };
    struct nas_elf_segment_result result={0};
    if(!nas_elf_segments_load(&elf,&policy,&ops,&result)) return false;
    *entry=result.entry;
    return true;
}
