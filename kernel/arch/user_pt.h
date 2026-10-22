#ifndef NASEED_USER_PT_BACKEND_H
#define NASEED_USER_PT_BACKEND_H
#include <stdint.h>
#include <stdbool.h>
/* Normalized user VM permissions: never expose ISA-specific descriptor bits. */
#define NAS_UP_READ  1U
#define NAS_UP_WRITE 2U
#define NAS_UP_EXEC  4U
struct nas_user_space {
    uint64_t root_pa;
    unsigned levels;
};
/* Low-level architecture PT primitive providers. The shared layer owns
 * DATA frames; arch PT implementations own only PAGE TABLE frames.
 * Walk visits only the lower (user) half, never Limine kernel HHDM.
 */
typedef bool (*nas_user_pt_visit)(uint64_t va, uint64_t pa,
                                  unsigned access, void *ctx);
unsigned nas_arch_pt_active_levels(void);
bool nas_arch_pt_new(struct nas_user_space *space, unsigned levels);
void nas_arch_pt_destroy(struct nas_user_space *space);
bool nas_arch_pt_map(const struct nas_user_space *space, uint64_t va,
                     uint64_t pa, unsigned access);
bool nas_arch_pt_lookup(const struct nas_user_space *space, uint64_t va,
                        uint64_t *pa, unsigned *access);
bool nas_arch_pt_unmap(const struct nas_user_space *space, uint64_t va);
bool nas_arch_pt_protect(const struct nas_user_space *space, uint64_t va,
                         unsigned access);
bool nas_arch_pt_walk(const struct nas_user_space *space,
                      nas_user_pt_visit visitor, void *ctx);
bool nas_arch_pt_install(const struct nas_user_space *space);
void nas_arch_pt_flush(uint64_t va);
#endif
