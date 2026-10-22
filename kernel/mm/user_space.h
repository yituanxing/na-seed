#ifndef NASEED_USER_SPACE_H
#define NASEED_USER_SPACE_H
#include "../arch/user_pt.h"
/* One implementation for mapping, page ownership, cloning and rollback
 * across RV64/ARM64. No user-visible kernel ABI or separate ELF loader. */
bool nas_space_new(struct nas_user_space *space, unsigned levels);
bool nas_space_delete(struct nas_user_space *space);
bool nas_space_clone(struct nas_user_space *dest,
                     const struct nas_user_space *source);
bool nas_space_map_page(struct nas_user_space *space, uint64_t va, bool writable);
/* Exact leaf permissions for ELF segment backends, no anonymous W+X. */
bool nas_space_map_access(struct nas_user_space *space, uint64_t va,
                          unsigned access);
bool nas_space_unmap_page(struct nas_user_space *space, uint64_t va);
bool nas_space_lookup(const struct nas_user_space *space, uint64_t va,
                      uint64_t *pa, unsigned *access);
bool nas_space_protect(struct nas_user_space *space, uint64_t va,
                       uint64_t pages, unsigned protection);
bool nas_space_user_writable(const struct nas_user_space *space, uint64_t va);
uint8_t *nas_space_user_byte(const struct nas_user_space *space, uint64_t va);
bool nas_space_regression(void);
#endif
