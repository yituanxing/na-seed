#ifndef NASEED_RISCV64_ELF_IMAGE_H
#define NASEED_RISCV64_ELF_IMAGE_H
#include <stdint.h>
#include <stdbool.h>
struct nas_user_space;
/* Thin RV64 VM adapter over the same PT_LOAD loader used by full execve.
 * Failure leaves rollback of the newly owned root to its caller. */
bool nas_rv_elf_load(struct nas_user_space *space,
                     const uint8_t *image,uint64_t size,uint64_t *entry);
#endif
