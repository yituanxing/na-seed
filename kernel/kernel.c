#include <limine.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * NaSeed M0: Limine handoff + real COM1 UART + physical frame bitmap.
 * Future ELF/syscall support must be driven by real userspace execution.
 * Bootloader reclaimable memory is NOT put into the frame allocator.
 */
#define USED_REQUEST __attribute__((used, section(".limine_requests")))
USED_REQUEST static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(6);
USED_REQUEST static volatile struct limine_hhdm_request hhdm_req = {
    .id = LIMINE_HHDM_REQUEST_ID, .revision = 0
};
USED_REQUEST static volatile struct limine_memmap_request mmap_req = {
    .id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0
};
USED_REQUEST static volatile struct limine_module_request module_req = {
    .id = LIMINE_MODULE_REQUEST_ID, .revision = 0
};
USED_REQUEST static volatile struct limine_framebuffer_request fb_req = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

/* Genuine x86 PC 16550-compatible COM1, using bounded polling. */
#define COM1 0x3f8
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static void serial_init(void) {
    outb(COM1 + 1, 0);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 1);      /* 115200 baud */
    outb(COM1 + 1, 0);
    outb(COM1 + 3, 3);      /* 8n1 */
    outb(COM1 + 2, 0xc7);
    outb(COM1 + 4, 0x0b);
}
static void serial_putc(char ch) {
    unsigned timeout = 1000000;
    while (timeout-- && !(inb(COM1 + 5) & 0x20)) {}
    if (inb(COM1 + 5) & 0x20) outb(COM1, (uint8_t)ch);
}
static void serial_puts(const char *str) {
    if (!str) return;
    while (*str) {
        if (*str == '\n') serial_putc('\r');
        serial_putc(*str++);
    }
}
static void serial_dec(uint64_t n) {
    char digits[21];
    unsigned used = 0;
    do {
        digits[used++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    while (used) serial_putc(digits[--used]);
}

/* Bitmap: bit 1 = unavailable, 0 = free. */
#define FRAME_BYTES 4096ULL
static uint8_t *bitmap;
static uint64_t bitmap_paddr, bitmap_frame_count, frame_count, free_frames;
static uint64_t cursor, hhdm;
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
__attribute__((noreturn)) static void halt(void) {
    for (;;) __asm__ volatile ("cli; hlt");
}

void kmain(void) {
    serial_init();
    serial_puts("[BOOT] NaSeed M0 kernel entered\n");
    if (!LIMINE_BASE_REVISION_SUPPORTED(base_revision) ||
        !hhdm_req.response || !mmap_req.response ||
        !mmap_req.response->entries || !mmap_req.response->entry_count) {
        serial_puts("[FAIL] Limine handoff\n");
        halt();
    }
    serial_puts("[BOOT] memory map entries=");
    serial_dec(mmap_req.response->entry_count);
    serial_puts(" modules=");
    serial_dec(module_req.response ? module_req.response->module_count : 0);
    serial_puts("\n");
    if (!frames_init(mmap_req.response, hhdm_req.response->offset)) {
        serial_puts("[FAIL] frame allocator init\n");
        halt();
    }
    uint64_t before = free_frames;
    serial_puts("[MM] available frames=");
    serial_dec(before);
    serial_puts("\n");
    uint64_t a = frame_alloc(), b = frame_alloc();
    if (!a || !b || a == b || !frame_free(a) || !frame_free(b) ||
        free_frames != before) {
        serial_puts("[FAIL] frame allocator smoke\n");
        halt();
    }
    serial_puts("[PASS] M0 memory allocator\n");
    serial_puts("[PASS] M0 boot / serial / memory\n");
    halt();
}
