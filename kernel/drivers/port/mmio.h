#ifndef NASEED_DRIVER_PORT_MMIO_H
#define NASEED_DRIVER_PORT_MMIO_H
#include <stdbool.h>
#include <stdint.h>

/* Byte offset within an already qualified mapping, before invoking the
 * provider. Subtraction avoids offset+4 wrapping at UINT32_MAX. */
static inline bool nas_mmio_u32_valid(uint32_t bytes,uint32_t offset) {
    return !(offset&3U) && offset<=bytes && bytes-offset>=4U;
}
#endif
