#ifndef NASEED_DISPLAY_FBCON_H
#define NASEED_DISPLAY_FBCON_H

#include <stdbool.h>
#include <stdint.h>

/* One Linux VT text plane, rendered through the existing DisplayDevice.
 * No framebuffer memory, VirtIO command queue or IRQ belongs to fbcon.
 * A graphical KD owner suspends painting while preserving the text plane;
 * KD_TEXT restores all cells from that plane onto physical scanout. */
void nas_fbcon_activate(bool enabled);
void nas_fbcon_set_graphics(bool graphics);
uint64_t nas_fbcon_write(const uint8_t *src,uint64_t length);

#endif
