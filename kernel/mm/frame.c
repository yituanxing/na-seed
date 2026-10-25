#include <limine.h>
#include <stdint.h>
#include <stdbool.h>
#include "frame.h"
/* Bitmap: bit 1 = unavailable, 0 = free. */
#define FRAME_BYTES 4096ULL
static uint8_t *bitmap;
static uint64_t bitmap_paddr, bitmap_frame_count, frame_count, free_frames;
static uint64_t cursor, hhdm;
/* Allocatable memory measured once at boot; free_frames tracks live usage. */
static uint64_t managed_frames;
static const struct limine_memmap_response *memory_map;

static bool region_end(uint64_t base, uint64_t length, uint64_t *end) {
    if (length > UINT64_MAX - base) return false;
    *end = base + length;
    return true;
}
static bool test_bit(uint64_t page) {
    return (bitmap[page >> 3] & (uint8_t)(1u << (page & 7))) != 0;
}
static void mark(uint64_t page) {
    bitmap[page >> 3] |= (uint8_t)(1u << (page & 7));
}
static void unmark(uint64_t page) {
    bitmap[page >> 3] &= (uint8_t)~(1u << (page & 7));
}
static uint64_t page_align_up(uint64_t n) {
    return (n + FRAME_BYTES - 1) & ~(FRAME_BYTES - 1);
}
static bool frames_init(const struct limine_memmap_response *map, uint64_t offset) {
    if (!map || !map->entries || !map->entry_count) return false;
    uint64_t top = 0;
    for (uint64_t i = 0; i < map->entry_count; i++) {
        const struct limine_memmap_entry *e = map->entries[i];
        uint64_t end;
        if (e && e->type == LIMINE_MEMMAP_USABLE &&
            region_end(e->base, e->length, &end) && end > top) top = end;
    }
    if (!top || top > UINT64_MAX - (FRAME_BYTES - 1)) return false;
    frame_count = page_align_up(top) / FRAME_BYTES;
    if (frame_count > UINT64_MAX - 7) return false;
    uint64_t bitmap_bytes = (frame_count + 7) / 8;
    if (bitmap_bytes > UINT64_MAX - (FRAME_BYTES - 1)) return false;
    bitmap_frame_count = page_align_up(bitmap_bytes) / FRAME_BYTES;
    if (bitmap_frame_count > UINT64_MAX / FRAME_BYTES) return false;
    uint64_t reserve_bytes = bitmap_frame_count * FRAME_BYTES;

    bool found = false;
    for (uint64_t i = 0; i < map->entry_count; i++) {
        const struct limine_memmap_entry *e = map->entries[i];
        uint64_t end;
        if (!e || e->type != LIMINE_MEMMAP_USABLE ||
            !region_end(e->base, e->length, &end) ||
            e->base > UINT64_MAX - (FRAME_BYTES - 1)) continue;
        uint64_t start = page_align_up(e->base);
        if (start <= end && reserve_bytes <= end - start) {
            bitmap_paddr = start;
            found = true;
            break;
        }
    }
    if (!found) return false;

    hhdm = offset;
    memory_map = map;
    bitmap = (uint8_t *)(uintptr_t)(hhdm + bitmap_paddr);
    for (uint64_t n = 0; n < reserve_bytes; n++) bitmap[n] = 0xff;

    free_frames = 0;
    for (uint64_t i = 0; i < map->entry_count; i++) {
        const struct limine_memmap_entry *e = map->entries[i];
        uint64_t end;
        if (!e || e->type != LIMINE_MEMMAP_USABLE ||
            !region_end(e->base, e->length, &end) ||
            e->base > UINT64_MAX - (FRAME_BYTES - 1)) continue;
        uint64_t begin = page_align_up(e->base) / FRAME_BYTES;
        uint64_t finish = end / FRAME_BYTES;
        for (uint64_t p = begin; p < finish && p < frame_count; p++) {
            if (test_bit(p)) { unmark(p); free_frames++; }
        }
    }
    uint64_t first_bitmap_page = bitmap_paddr / FRAME_BYTES;
    for (uint64_t p = first_bitmap_page; p < first_bitmap_page + bitmap_frame_count; p++) {
        if (!test_bit(p)) { mark(p); free_frames--; }
    }
    /* Reserve address zero: failure sentinel for future allocation calls. */
    if (!test_bit(0)) { mark(0); free_frames--; }
    cursor = 1;
    managed_frames = free_frames;
    return true;
}
static uint64_t take_frame(uint64_t page) {
    mark(page);
    free_frames--;
    cursor = page + 1;
    uint64_t paddr = page * FRAME_BYTES;
    volatile uint64_t *ptr = (volatile uint64_t *)(uintptr_t)(hhdm + paddr);
    for (unsigned i = 0; i < FRAME_BYTES / 8; i++) ptr[i] = 0;
    return paddr;
}
static uint64_t frame_alloc(void) {
    if (!bitmap || !free_frames) return 0;
    for (uint64_t p = cursor; p < frame_count; p++)
        if (!test_bit(p)) return take_frame(p);
    for (uint64_t p = 1; p < cursor && p < frame_count; p++)
        if (!test_bit(p)) return take_frame(p);
    return 0;
}
static bool frame_free(uint64_t paddr) {
    if (!bitmap || paddr == 0 || paddr % FRAME_BYTES) return false;
    uint64_t p = paddr / FRAME_BYTES;
    if (p >= frame_count || !test_bit(p)) return false;
    uint64_t first = bitmap_paddr / FRAME_BYTES;
    if (p >= first && p - first < bitmap_frame_count) return false;
    bool is_ram = false;
    for (uint64_t i = 0; i < memory_map->entry_count; i++) {
        const struct limine_memmap_entry *e = memory_map->entries[i];
        uint64_t end;
        if (e && e->type == LIMINE_MEMMAP_USABLE &&
            region_end(e->base, e->length, &end) &&
            paddr >= e->base && paddr <= end && FRAME_BYTES <= end - paddr) {
            is_ram = true;
            break;
        }
    }
    if (!is_ram) return false;
    unmark(p);
    free_frames++;
    if (p < cursor) cursor = p;
    return true;
}

bool nas_frames_init(const struct limine_memmap_response *map, uint64_t off) {
    return frames_init(map, off);
}
uint64_t nas_free_frames(void) { return free_frames; }
uint64_t nas_managed_frames(void) { return managed_frames; }
uint64_t nas_frame_alloc(void) { return frame_alloc(); }
bool nas_frame_free(uint64_t physical) { return frame_free(physical); }
uint64_t nas_hhdm_offset(void) { return hhdm; }
