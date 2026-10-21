#ifndef NASEED_RISCV64_ELF_IMAGE_H
#define NASEED_RISCV64_ELF_IMAGE_H
#include <stdint.h>
#include <stdbool.h>
#include "mmu.h"

/* Validated static RV64 ET_EXEC PT_LOAD module into genuine user PTEs.
 * This is an initial per-architecture ELF ABI adapter, not a replacement
 * for the mature x86 loader, execve, or a cross-target dynamic linker.
 * A failed load rolls back mappings and releases its newly owned frames. */
bool nas_rv_elf_load(struct nas_rv_page_table *pt,
                     const uint8_t *image, uint64_t size,
                     uint64_t *entry);
#endif
