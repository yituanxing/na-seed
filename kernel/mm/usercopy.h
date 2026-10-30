#ifndef NASEED_MM_USERCOPY_H
#define NASEED_MM_USERCOPY_H
#include <stdint.h>
#include <stdbool.h>
/* MM address check and user-page access; current implementation is single-core. */
bool nas_user_copy_to_mapped(uint64_t addr, const uint8_t *src, uint64_t len);
/* Linux user pointer permissions are VMA policy, not the current COW
 * hardware PTE writable bit. This inspection never commits a mapping. */
bool nas_user_address_writable(uint64_t addr);
bool nas_user_read_bytes(uint64_t addr, uint8_t *dst, uint64_t len);
/* Validate/materialize every source page before an atomic backend write.
 * No user data is consumed; callers must not schedule between preparation
 * and transferring the same range to a kernel-owned provider. */
bool nas_user_prepare_read(uint64_t addr, uint64_t len);
/* Read at most capacity bytes, including the terminating NUL. Resolves
 * legitimate lazy pages one at a time and never reads beyond the first NUL.
 * Returns 0, -EFAULT, or -ENAMETOOLONG. On failure dst may be partial. */
int nas_user_read_cstr(uint64_t addr, char *dst, uint64_t capacity);
/* Resolve lazy/COW pages and validate the entire user output range BEFORE a
 * syscall publishes an FD or consumes input. No user bytes are modified;
 * preparatory PTE/COW changes can remain if a later page fails. The caller
 * must not schedule/mutate the MM between preparation and its final copy. */
bool nas_user_prepare_write(uint64_t addr, uint64_t len);
bool nas_user_write_bytes(uint64_t addr, const uint8_t *src, uint64_t len);
bool nas_user_read_u64(uint64_t addr, uint64_t *result);
bool nas_user_write_u64(uint64_t addr, uint64_t value);
#endif
