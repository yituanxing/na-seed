#ifndef NASEED_KERNEL_LIB_ENDIAN_H
#define NASEED_KERNEL_LIB_ENDIAN_H
#include <stdint.h>

/* Unaligned byte buffers only; not volatile MMIO or native register access. */
static inline uint16_t nas_get_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1]<<8));
}
static inline uint32_t nas_get_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static inline uint64_t nas_get_le64(const uint8_t *p) {
    return (uint64_t)nas_get_le32(p) | ((uint64_t)nas_get_le32(p+4)<<32);
}
static inline void nas_put_le16(uint8_t *p,uint16_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);
}
static inline void nas_put_le32(void *dst,uint32_t v) {
    uint8_t *p=dst;
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);
}
static inline void nas_put_le64(uint8_t *p,uint64_t v) {
    nas_put_le32(p,(uint32_t)v);nas_put_le32(p+4,(uint32_t)(v>>32));
}
static inline uint32_t nas_get_be32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24) | ((uint32_t)p[1]<<16) |
           ((uint32_t)p[2]<<8) | (uint32_t)p[3];
}
static inline void nas_put_be32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);
    p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;
}
static inline void nas_put_be64(uint8_t *p,uint64_t v) {
    nas_put_be32(p,(uint32_t)(v>>32));nas_put_be32(p+4,(uint32_t)v);
}
#endif
