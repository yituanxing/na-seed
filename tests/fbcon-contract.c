#include "../kernel/drivers/display/fbcon.h"
#include "../kernel/drivers/display/fbdev.h"
#include "../kernel/mm/heap.h"
#include "../kernel/errno.h"
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

static uint32_t screen[600][800], before[600][800];
static uint8_t report[32];
static size_t report_len;
static unsigned allocations;
void *nas_heap_alloc(size_t size) {
    void *p = malloc(size);
    if (p) ++allocations;
    return p;
}
bool nas_heap_free(void *p) {
    if (p) { --allocations; free(p); }
    return true;
}
static void sink(const uint8_t *p, uint64_t n) {
    assert(n <= sizeof(report) - report_len);
    memcpy(report + report_len, p, n);
    report_len += n;
}
static void write_text(const char *s) {
    assert(nas_fbcon_write((const uint8_t *)s, strlen(s)) == strlen(s));
}
int main(void) {
    struct nas_fb_boot_info boot = {
        .address = (volatile uint8_t *)screen, .width = 800, .height = 600,
        .pitch = sizeof(screen[0]), .bpp = 32, .memory_model = 1,
        .red_size = 8, .red_shift = 16, .green_size = 8, .green_shift = 8,
        .blue_size = 8, .blue_shift = 0
    };
    assert(nas_fbcon_write((const uint8_t *)"x", 1) == (uint64_t)-ENODEV);
    assert(nas_fbdev_boot_init(&boot));
    nas_fbcon_set_report_sink(sink);
    nas_fbcon_activate(true);
    uint16_t cols, rows;
    assert(nas_fbcon_dimensions(&cols, &rows) && cols == 80 && rows == 25);
    write_text("\033[2;3H\033[6n\033[5n");
    assert(report_len == 10 && !memcmp(report, "\033[2;3R\033[0n", 10));
    memcpy(before, screen, sizeof(screen));
    write_text("ABC\n");
    assert(memcmp(before, screen, sizeof(screen)));
    nas_fbcon_set_graphics(true);
    memset(screen, 0x5a, sizeof(screen));
    memcpy(before, screen, sizeof(screen));
    write_text("\033[31mhidden\033[0m");
    assert(!memcmp(before, screen, sizeof(screen)));
    nas_fbcon_set_graphics(false);
    assert(memcmp(before, screen, sizeof(screen)));
    assert(screen[0][0] == 0);
    nas_fbcon_activate(false);
    assert(!allocations && !nas_fbcon_dimensions(&cols, &rows));
    nas_fbcon_activate(true);
    assert(nas_fbcon_dimensions(&cols, &rows));
    nas_fbcon_activate(false);
    assert(!allocations);
    return 0;
}
