#ifndef NASEED_RANDOM_H
#define NASEED_RANDOM_H
#include <stdint.h>
#include <stdbool.h>
/* Optional best-effort 256-bit Limine handoff, not certified entropy. */
bool nas_random_seed(const uint64_t *values,uint64_t value_count);
bool nas_random_bytes(uint8_t *dst,uint64_t len);
uint64_t nas_random_getrandom(uint64_t addr,uint64_t len,uint64_t flags);
#endif
