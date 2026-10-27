#ifndef NASEED_LWEXT4_FREESTANDING_PLATFORM_H
#define NASEED_LWEXT4_FREESTANDING_PLATFORM_H
#include <stddef.h>
/* Compile unchanged upstream lwext4 without hosted libc or generated CMake
 * headers. The kernel provides memory and string routines. Avoid stdout
 * diagnostics in the preemption/IRQ path; retain errno and file formats. */
#define CONFIG_USE_DEFAULT_CFG 1
#define CONFIG_HAVE_OWN_ERRNO 1
#define CONFIG_HAVE_OWN_OFLAGS 1
#define CONFIG_USE_USER_MALLOC 1
#define CONFIG_DEBUG_PRINTF 0
#define CONFIG_DEBUG_ASSERT 0
void *ext4_user_malloc(size_t size);
void *ext4_user_calloc(size_t count,size_t size);
void *ext4_user_realloc(void *ptr,size_t size);
void ext4_user_free(void *ptr);
#endif
