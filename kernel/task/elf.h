#ifndef NASEED_TASK_ELF_H
#define NASEED_TASK_ELF_H
#include <stdint.h>
#include <stdbool.h>
struct __attribute__((packed)) elf_hdr {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, phoff, shoff;
    uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct __attribute__((packed)) elf_phdr {
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz, align;
};
struct elf_image {
    const uint8_t *data;
    uint64_t size;
};
bool nas_elf_load(const struct elf_image *image,
                  const struct elf_image *interpreter, uint64_t *entry);
#endif
