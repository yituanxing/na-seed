#ifndef NASEED_LWIP_ARCH_CC_H
#define NASEED_LWIP_ARCH_CC_H
/* Compiler/platform ABI for the pinned official lwIP 2.2.1 sources.
 * Protocol code stays in the submodule, never in this port. */
#include <stdint.h>
#include <stddef.h>
#define BYTE_ORDER LITTLE_ENDIAN
#define LWIP_NO_STDIO 1
#define LWIP_PLATFORM_DIAG(x) do { } while (0)
void nas_lwip_assert_failed(const char *reason);
uint32_t nas_lwip_random32(void);
#define LWIP_PLATFORM_ASSERT(x) nas_lwip_assert_failed(x)
#define LWIP_RAND() nas_lwip_random32()
#endif
