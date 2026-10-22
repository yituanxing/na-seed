#ifndef NASEED_USER_ELF_H
#define NASEED_USER_ELF_H
#include <stdint.h>
#include <stdbool.h>
struct nas_user_space;
/* Linux native-ELF architecture-independent address space target.
 * These limits describe a *test workload* footprint, not a new ISA loader.
 * Full kernel execve uses the SAME ELF segment implementation below.
 */
struct nas_user_elf_limits {
    uint64_t min_va, max_va, max_image_bytes;
    unsigned max_phnum, max_pages;
    bool allow_dynamic;
};
bool nas_user_elf_load(struct nas_user_space *space,
                       const uint8_t *image, uint64_t bytes,
                       const struct nas_user_elf_limits *limits,
                       uint64_t *entry);
#endif
