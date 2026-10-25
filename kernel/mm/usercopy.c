#include <stdint.h>
#include <stdbool.h>
#include "../arch/mmu.h"
#include "../lib/string.h"
#include "usercopy.h"
/* Every NaSeed user page is a 4-KiB base page across all three backends.
 * A resolved HHDM pointer is physically contiguous only until the page
 * boundary; do not confuse adjacent user VAs with adjacent frames. */
#define NAS_USERCOPY_PAGE_BYTES 4096ULL
static uint64_t page_chunk(uint64_t va, uint64_t remaining) {
    uint64_t room = NAS_USERCOPY_PAGE_BYTES -
                    (va & (NAS_USERCOPY_PAGE_BYTES - 1));
    return remaining < room ? remaining : room;
}
/* This kernel-origin copy may initialize RX code pages: intentionally
 * require a valid user mapping, NOT user-writable permission. */
bool nas_user_copy_to_mapped(uint64_t va, const uint8_t *data, uint64_t length) {
    if (length && va > UINT64_MAX - (length - 1)) return false;
    for (uint64_t done = 0; done < length;) {
        uint64_t addr = va + done;
        uint8_t *dst = nas_mmu_user_byte(addr);
        if (!dst) return false;
        uint64_t n = page_chunk(addr, length - done);
        memcpy(dst, data + done, (size_t)n);
        done += n;
    }
    return true;
}
bool nas_user_read_bytes(uint64_t addr, uint8_t *dst, uint64_t len) {
    if (addr >= USER_LIMIT || len > USER_LIMIT - addr) return false;
    for (uint64_t done = 0; done < len;) {
        uint64_t at = addr + done;
        uint8_t *src = nas_mmu_user_byte(at);
        if (!src) return false;
        uint64_t n = page_chunk(at, len - done);
        memcpy(dst + done, src, (size_t)n);
        done += n;
    }
    return true;
}
bool nas_user_write_bytes(uint64_t addr, const uint8_t *src, uint64_t len) {
    if (addr >= USER_LIMIT || len > USER_LIMIT - addr) return false;
    /* Keep original two-phase semantics: reject any unmapped/read-only
     * page before modifying ANY byte of the target range. */
    for (uint64_t checked = 0; checked < len;) {
        uint64_t at = addr + checked;
        if (!nas_mmu_user_writable(at)) return false;
        checked += page_chunk(at, len - checked);
    }
    for (uint64_t done = 0; done < len;) {
        uint64_t at = addr + done;
        uint8_t *dest = nas_mmu_user_byte(at);
        if (!dest) return false;
        uint64_t n = page_chunk(at, len - done);
        memcpy(dest, src + done, (size_t)n);
        done += n;
    }
    return true;
}
bool nas_user_read_u64(uint64_t addr, uint64_t *value) {
    uint8_t bytes[8];
    if (!nas_user_read_bytes(addr, bytes, 8)) return false;
    *value = 0;
    for (unsigned n = 0; n < 8; n++) *value |= (uint64_t)bytes[n] << (8 * n);
    return true;
}
bool nas_user_write_u64(uint64_t addr, uint64_t value) {
    uint8_t bytes[8];
    for (unsigned n = 0; n < 8; n++) bytes[n] = (uint8_t)(value >> (8 * n));
    return nas_user_write_bytes(addr, bytes, 8);
}
