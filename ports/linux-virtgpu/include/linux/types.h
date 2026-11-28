#ifndef NAS_PORT_LINUX_VIRTGPU_TYPES_H
#define NAS_PORT_LINUX_VIRTGPU_TYPES_H
/* UAPI-only platform typedef adapter for compiling the ORIGINAL Linux
 * v6.14 VirtIO GPU/DRM headers across NaSeed's three freestanding targets.
 * Do not expand this shim into kernel DRM, locking, or a fake GPU driver. */
#include <stdint.h>
#include <stddef.h>
typedef uint8_t __u8;
typedef int8_t __s8;
typedef uint16_t __u16;
typedef int16_t __s16;
typedef uint32_t __u32;
typedef int32_t __s32;
typedef uint64_t __u64;
typedef int64_t __s64;
typedef uint16_t __le16;
typedef uint32_t __le32;
typedef uint64_t __le64;
typedef size_t __kernel_size_t;
/* Sparse-only address-space qualifier: native compiler has no __user. */
#ifndef __user
#define __user
#endif
#endif
