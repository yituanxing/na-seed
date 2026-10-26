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
bool nas_user_write_bytes(uint64_t addr, const uint8_t *src, uint64_t len);
bool nas_user_read_u64(uint64_t addr, uint64_t *result);
bool nas_user_write_u64(uint64_t addr, uint64_t value);
#endif
