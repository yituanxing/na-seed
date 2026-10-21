#ifndef NASEED_TASK_ELF_FORMAT_H
#define NASEED_TASK_ELF_FORMAT_H
#include <stdint.h>
#include <stdbool.h>
#include "elf.h"
/* One shared ELF64 validator. Byte source, VM mappings, and ABI-specific
 * permissions are distinct interfaces; fail before changing any page table. */
struct nas_elf_view {
    const struct elf_hdr *header;
    const struct elf_phdr *phdrs;
};
static inline bool nas_elf_range_in_file(uint64_t offset, uint64_t count,
                                         uint64_t image_size) {
    return offset <= image_size && count <= image_size - offset;
}
static inline bool nas_elf_open(struct nas_elf_view *view,
                                const uint8_t *bytes, uint64_t size,
                                uint16_t machine, bool allow_dyn,
                                unsigned max_phnum) {
    if (!view || !bytes || size < sizeof(struct elf_hdr)) return false;
    view->header = 0;
    view->phdrs = 0;
    const struct elf_hdr *h = (const struct elf_hdr *)(const void *)bytes;
    if (h->ident[0] != 0x7f || h->ident[1] != 'E' ||
        h->ident[2] != 'L' || h->ident[3] != 'F' ||
        h->ident[4] != 2 || h->ident[5] != 1 || h->ident[6] != 1 ||
        (h->type != 2 && (!allow_dyn || h->type != 3)) ||
        h->machine != machine || h->version != 1 ||
        h->phentsize != sizeof(struct elf_phdr) ||
        !h->phnum || h->phnum > max_phnum ||
        !nas_elf_range_in_file(h->phoff,
                              (uint64_t)h->phnum * sizeof(struct elf_phdr),
                              size))
        return false;
    view->header = h;
    view->phdrs = (const struct elf_phdr *)(const void *)(bytes + h->phoff);
    return true;
}
static inline bool nas_elf_segment_file_ok(const struct elf_phdr *ph,
                                           uint64_t image_size) {
    return ph && ph->filesz <= ph->memsz &&
           nas_elf_range_in_file(ph->offset, ph->filesz, image_size);
}
#endif
