#include <stdint.h>
#include "../arch/abi.h"
#include <stdbool.h>
#include "../task.h"
#include "../arch/mmu.h"
#include "../mm/usercopy.h"
#include "elf.h"
#include "elf_format.h"
#include "elf_segments.h"
extern void nas_print(const char *);
extern void nas_hex(uint64_t);
#define TASK (*nas_current_task)
#define heap_break TASK.heap_break
#define loaded_phdr TASK.loaded_phdr
#define loaded_phnum TASK.loaded_phnum
#define loaded_main_entry TASK.loaded_main_entry
#define loaded_interp_base TASK.loaded_interp_base
#define USER_STACK_TOP NAS_USER_STACK_TOP

/* Full kernel and RV64 hardware-smoke use the SAME ELF PT_LOAD loader.
 * All architectural differences are memory-map and copy callbacks.
 */
#define ALPINE_APP_BASE NAS_ELF_DYN_APP_BASE
#define ALPINE_LDSO_BASE NAS_ELF_DYN_INTERP_BASE
static bool elf_kernel_map(void *ignored,uint64_t va,unsigned flags) {
    (void)ignored;
    return nas_mmu_map_page(va,(flags & 2U)!=0);
}
static bool elf_kernel_copy(void *ignored,uint64_t va,
                            const uint8_t *src,uint64_t len) {
    (void)ignored;
    return nas_user_copy_to_mapped(va,src,len);
}
static bool elf_map_image(const struct elf_image *image,uint64_t bias,
                          bool main_image,uint64_t *entry,bool *has_interp) {
    struct nas_elf_segment_policy options={
        .machine=NAS_ELF_MACHINE,
        .allow_dynamic_elf=true,
        .max_phnum=64,
        .bias=bias,
        .min_va=0x10000,
        .max_va=USER_STACK_TOP-0x100000,
        .max_segment=64ULL<<20,
        .interpreter=main_image?NAS_ELF_INTERP:0
    };
    struct nas_elf_segment_ops op={
        .ctx=0,.map_page=elf_kernel_map,.copy_bytes=elf_kernel_copy
    };
    struct nas_elf_segment_result result={0};
    if(!nas_elf_segments_load(image,&options,&op,&result)) {
        /* Rejected original ELF is never silently rewritten as another
         * program. Emit bounded native ELF diagnostics on ALL ISAs. */
        struct elf_hdr h;
        if (nas_elf_image_open(image,NAS_ELF_MACHINE,true,64,&h)) {
            nas_print("[ELF REJECT] load segments phnum="); nas_hex(h.phnum);
            nas_print(" type="); nas_hex(h.type);
            nas_print(" entry="); nas_hex(h.entry);
            nas_print(" size="); nas_hex(image->size); nas_print("\n");
            for (unsigned i=0;i<h.phnum;i++) {
                struct elf_phdr ph;
                if (!nas_elf_image_read(image,h.phoff+(uint64_t)i*sizeof(ph),
                                        &ph,sizeof(ph))) break;
                if (ph.type!=1U && ph.type!=3U) continue;
                nas_print("[ELF REJECT] PH type="); nas_hex(ph.type);
                nas_print(" flags="); nas_hex(ph.flags);
                nas_print(" off="); nas_hex(ph.offset);
                nas_print(" va="); nas_hex(ph.vaddr);
                nas_print(" file="); nas_hex(ph.filesz);
                nas_print(" mem="); nas_hex(ph.memsz);
                nas_print("\n");
            }
        } else nas_print("[ELF REJECT] invalid header\n");
        return false;
    }
    *entry=result.entry;
    *has_interp=result.has_interp;
    if(main_image) {
        loaded_phdr=result.phdr;
        loaded_phnum=result.phnum;
        if(result.end>heap_break) heap_break=result.end;
    }
    return true;
}

bool nas_elf_load(const struct elf_image *image,
                     const struct elf_image *interpreter, uint64_t *entry) {
    struct elf_hdr header;
    if (!nas_elf_image_open(image, NAS_ELF_MACHINE, true, 64, &header))
        return false;
    const struct elf_hdr *e = &header;
    uint64_t bias = e->type == 3 ? ALPINE_APP_BASE : 0;
    bool has_interp = false;
    if (!elf_map_image(image, bias, true, &loaded_main_entry, &has_interp))
        return false;
    *entry = loaded_main_entry;
    if (!has_interp) return true;
    if (!interpreter || !interpreter->data) return false;
    loaded_interp_base = ALPINE_LDSO_BASE;
    bool nested_interp = false;
    if (!elf_map_image(interpreter, loaded_interp_base, false, entry, &nested_interp))
        return false;
    nas_print("[DYN] real musl interpreter loaded, entry=");
    nas_hex(*entry); nas_print("\n");
    return true;
}
