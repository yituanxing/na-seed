#include <stdint.h>
#include <stdbool.h>
#include "../arch/mmu.h"
#include "usercopy.h"
/* One checked user-access implementation shared by FS, task, ELF and ABI. */

bool nas_user_copy_to_mapped(uint64_t va, const uint8_t *data, uint64_t length) {
    for (uint64_t i = 0; i < length; i++) {
        uint8_t *dst = nas_mmu_user_byte(va + i);
        if (!dst) return false;
        *dst = data[i];
    }
    return true;
}

bool nas_user_read_bytes(uint64_t addr, uint8_t *dst, uint64_t len) {
    if (addr >= USER_LIMIT || len > USER_LIMIT - addr) return false;
    for (uint64_t n = 0; n < len; n++) {
        uint8_t *p = nas_mmu_user_byte(addr + n);
        if (!p) return false;
        dst[n] = *p;
    }
    return true;
}
bool nas_user_write_bytes(uint64_t addr, const uint8_t *src, uint64_t len) {
    if (addr >= USER_LIMIT || len > USER_LIMIT - addr) return false;
    for (uint64_t n = 0; n < len; n++) {
        if (!nas_mmu_user_writable(addr + n))
            return false;
    }
    for (uint64_t n = 0; n < len; n++)
        *nas_mmu_user_byte(addr + n) = src[n];
    return true;
}
bool nas_user_nas_user_read_u64(uint64_t addr, uint64_t *value) {
    uint8_t bytes[8];
    if (!nas_user_read_bytes(addr, bytes, 8)) return false;
    *value = 0;
    for (unsigned n = 0; n < 8; n++) *value |= (uint64_t)bytes[n] << (8 * n);
    return true;
}
bool nas_user_nas_user_write_u64(uint64_t addr, uint64_t value) {
    uint8_t bytes[8];
    for (unsigned n = 0; n < 8; n++) bytes[n] = (uint8_t)(value >> (8 * n));
    return nas_user_write_bytes(addr, bytes, 8);
}
