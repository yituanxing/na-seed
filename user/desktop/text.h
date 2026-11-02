#ifndef NASEED_DESKTOP_TEXT_H
#define NASEED_DESKTOP_TEXT_H
#include <stddef.h>
#include <stdint.h>
/* Small native text rasterizer for client-owned BGRA surfaces.  It is an
 * ordinary userspace library, never a framebuffer or terminal driver. */
void nasd_draw_text(uint8_t *,unsigned,unsigned,unsigned,unsigned,
                    const char *,size_t,uint8_t,uint8_t,uint8_t);
#endif
