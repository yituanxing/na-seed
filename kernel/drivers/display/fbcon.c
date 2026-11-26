/* SPDX-License-Identifier: GPL-2.0-only */
#include "fbcon.h"
#include "display.h"
#include "../../mm/heap.h"
#include "../../errno.h"
#include "../../lib/string.h"
#include "../../../third_party/flanterm/src/flanterm.h"
#include "../../../third_party/flanterm/src/flanterm_backends/fb.h"

/* Flanterm owns every terminal cell, escape sequence and glyph. The bounded
 * shadow surface is only scanout transport: no direct access to boot or DMA
 * framebuffer storage, and no painting over a graphical KD owner.
 * Keep the historical 25-row viewport and cap columns at 80 (9px glyphs). */
#define TEXT_WIDTH 720U
#define TEXT_HEIGHT 400U
#define DISPLAY_MAX_WIDTH 1920U
static uint32_t shadow[TEXT_HEIGHT][TEXT_WIDTH];
static uint32_t presented[TEXT_HEIGHT][TEXT_WIDTH];
static const uint32_t blank[DISPLAY_MAX_WIDTH];
static struct flanterm_context *terminal;
static const struct nas_display_device *owner;
static size_t width, height, left, top, pitch;
static bool graphics, repaint;
static void (*report_sink)(const uint8_t *, uint64_t);

static void release(void *ptr, size_t size) {
    (void)size;
    (void)nas_heap_free(ptr);
}

static bool publish(void) {
    if (graphics) return true;
    if (!owner || nas_display_primary() != owner) return false;
    if (repaint) {
        /* Clear only the surrounding margins; the text area is copied below. */
        for (size_t y = 0; y < owner->mode.height; ++y) {
            size_t before = y < top || y >= top + height ?
                            owner->mode.width : left;
            size_t after = y >= top && y < top + height ?
                           owner->mode.width - left - width : 0;
            uint64_t off = (uint64_t)y * pitch;
            if (before && nas_display_write_at(off, (const uint8_t *)blank,
                    before * 4U) != before * 4U) return false;
            if (after && nas_display_write_at(off + (left + width) * 4U,
                    (const uint8_t *)blank, after * 4U) != after * 4U)
                return false;
        }
    }
    for (size_t y = 0; y < height; ++y) {
        size_t first = 0, end = width;
        if (!repaint) {
            while (first < end && shadow[y][first] == presented[y][first])
                ++first;
            while (end > first && shadow[y][end - 1] == presented[y][end - 1])
                --end;
        }
        if (first == end) continue;
        size_t bytes = (end - first) * sizeof(uint32_t);
        uint64_t off = (uint64_t)(top + y) * pitch + (left + first) * 4U;
        if (nas_display_write_at(off, (const uint8_t *)&shadow[y][first],
                                 bytes) != bytes) return false;
        memcpy(&presented[y][first], &shadow[y][first], bytes);
    }
    repaint = false;
    return true;
}

static unsigned decimal(uint8_t *out, uint64_t n) {
    /* Flanterm coordinates are bounded by our viewport, at most 80 x 25. */
    unsigned count = 0;
    if (n >= 10U) out[count++] = (uint8_t)('0' + n / 10U);
    out[count++] = (uint8_t)('0' + n % 10U);
    return count;
}
static void event(struct flanterm_context *ctx, uint64_t id, uint64_t a,
                  uint64_t b, uint64_t c) {
    (void)ctx; (void)c;
    if (!report_sink) return;
    uint8_t response[12] = {27, '['};
    unsigned n = 2;
    if (id == FLANTERM_CB_STATUS_REPORT) {
        response[n++] = '0'; response[n++] = 'n';
    } else if (id == FLANTERM_CB_POS_REPORT && a <= 80U && b <= 25U) {
        n += decimal(response + n, b);
        response[n++] = ';';
        n += decimal(response + n, a);
        response[n++] = 'R';
    } else return;
    report_sink(response, n);
}

void nas_fbcon_set_report_sink(void (*sink)(const uint8_t *, uint64_t)) {
    report_sink = sink;
}
void nas_fbcon_activate(bool enabled) {
    if (terminal) flanterm_deinit(terminal, release);
    terminal = NULL;
    owner = NULL;
    graphics = false;
    if (!enabled) return;
    const struct nas_display_device *device = nas_display_primary();
    if (!device) return;
    const struct nas_display_mode *m = &device->mode;
    if (m->bpp != 32U || m->width < 640U || m->height < TEXT_HEIGHT ||
        m->width > DISPLAY_MAX_WIDTH || m->pitch < m->width * 4U ||
        m->red_size != 8U || m->green_size != 8U || m->blue_size != 8U)
        return;
    width = m->width < TEXT_WIDTH ? m->width : TEXT_WIDTH;
    height = TEXT_HEIGHT;
    left = (m->width - width) / 2U;
    top = (m->height - height) / 2U;
    pitch = m->pitch;
    uint32_t white = 0x00ffffffU;
    terminal = flanterm_fb_init(nas_heap_alloc, release, &shadow[0][0],
        width, height, sizeof(shadow[0]),
        m->red_size, m->red_shift, m->green_size, m->green_shift,
        m->blue_size, m->blue_shift,
        NULL, NULL, NULL, NULL, &white, NULL, NULL,
        NULL, 0, 0, 1, 1, 1, 0, FLANTERM_FB_ROTATE_0);
    if (!terminal) return;
    owner = device;
    flanterm_set_autoflush(terminal, false);
    flanterm_set_callback(terminal, event);
    repaint = true;
    (void)publish();
}
bool nas_fbcon_dimensions(uint16_t *cols, uint16_t *rows) {
    if (!terminal || !cols || !rows) return false;
    size_t x, y;
    flanterm_get_dimensions(terminal, &x, &y);
    *cols = (uint16_t)x; *rows = (uint16_t)y;
    return true;
}
void nas_fbcon_set_graphics(bool enabled) {
    if (!terminal || graphics == enabled) return;
    graphics = enabled;
    if (!enabled) {
        repaint = true;
        (void)publish();
    }
}
uint64_t nas_fbcon_write(const uint8_t *src, uint64_t length) {
    if (!terminal || nas_display_primary() != owner) return (uint64_t)-ENODEV;
    if (!src && length) return (uint64_t)-EFAULT;
    if (!length) return 0;
    /* Preserve the historical console's LF -> CRLF output convention.
     * The ANSI parser, screen state and drawing remain entirely upstream. */
    size_t start = 0;
    for (size_t i = 0; i < length; ++i) {
        if (src[i] != '\n') continue;
        if (i > start) flanterm_write(terminal, (const char *)src + start, i - start);
        flanterm_write(terminal, "\r\n", 2);
        start = i + 1;
    }
    if (start < length)
        flanterm_write(terminal, (const char *)src + start, length - start);
    flanterm_flush(terminal);
    return publish() ? length : (uint64_t)-EIO;
}
