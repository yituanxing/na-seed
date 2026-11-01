#ifndef NASEED_LWIP_ARCH_CC_H
#define NASEED_LWIP_ARCH_CC_H
/* Compiler/platform ABI for the pinned official lwIP 2.2.1 sources.
 * Protocol code stays in the submodule, never in this port. */
#include <stdint.h>
#include <stddef.h>
#define BYTE_ORDER LITTLE_ENDIAN
/* NaSeed's three kernel targets are freestanding LP64. Clang's generic
 * <inttypes.h> can fall through to target glibc bits/wordsize.h, which does
 * not exist in the none-elf kernel sysroot. Use upstream lwIP's supported
 * LWIP_NO_INTTYPES_H port hook; keep all protocol and formatter semantics
 * in the official sources. Never pull host libc headers into kernel code. */
_Static_assert(sizeof(unsigned int)==4 && sizeof(unsigned long)==8,
               "lwIP kernel port requires LP64");
#define LWIP_NO_INTTYPES_H 1
#define X8_F "02x"
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"
#define SZT_F "lu"
/* No host libc limits/ctype/unistd in --target=*-none-elf builds. The
 * upstream arch.h fallback supplies a bounded 32-bit ssize_t and portable
 * character predicates; all three kernel ISAs are LP64. */
#define LWIP_NO_LIMITS_H 1
#define INT_MAX 2147483647
#define LWIP_NO_CTYPE_H 1
#define LWIP_NO_STDIO 1
#define LWIP_PLATFORM_DIAG(x) do { } while (0)
void nas_lwip_assert_failed(const char *reason);
uint32_t nas_lwip_random32(void);
#define LWIP_PLATFORM_ASSERT(x) nas_lwip_assert_failed(x)
#define LWIP_RAND() nas_lwip_random32()
#endif
