#ifndef NASEED_USER_SPACE_H
#define NASEED_USER_SPACE_H
#include "../arch/user_pt.h"
/* One low-level page-table implementation across RV64/ARM64.
 * Production fork and COW policy belong exclusively to mm.c. */
bool nas_space_new(struct nas_user_space *space, unsigned levels);
bool nas_space_delete(struct nas_user_space *space);
bool nas_space_map_page(struct nas_user_space *space, uint64_t va, bool writable);
/* Exact typed leaf permissions (including Linux anonymous RWX); the VM
 * owner chooses which prot and mapping kinds are authorized. */
bool nas_space_map_access(struct nas_user_space *space, uint64_t va,
                          unsigned access);
bool nas_space_map_elf_access(struct nas_user_space *space, uint64_t va,
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
