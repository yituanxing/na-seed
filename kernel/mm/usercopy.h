#ifndef NASEED_MM_USERCOPY_H
#define NASEED_MM_USERCOPY_H
#include <stdint.h>
#include <stdbool.h>
/* Transitional: checked copy implementation stays in m1.c until MM usercopy extraction. */
bool nas_user_write_bytes(uint64_t addr, const uint8_t *src, uint64_t len);
#endif
