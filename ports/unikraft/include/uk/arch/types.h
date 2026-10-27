#ifndef NASEED_UNIKRAFT_PORT_ARCH_TYPES_H
#define NASEED_UNIKRAFT_PORT_ARCH_TYPES_H
/* The vendored Unikraft VirtIO ABI header uses the Linux-style __uN
 * aliases. Keep the upstream header byte-for-byte untouched. */
#include <stdint.h>
typedef uint8_t __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
#ifndef __may_alias
#define __may_alias __attribute__((__may_alias__))
#endif
#endif
