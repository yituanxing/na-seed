/* Pure host framebuffer mock: renderer pixel/graphics ownership checks.
 * The strict guest proof separately samples the real VirtIO-GPU via QMP. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "kernel/drivers/display/fbcon.h"
#include "kernel/drivers/display/fbdev.h"

#define W 800U
#define H 600U
static uint8_t pixels[W*H*4U];
static uint8_t snapshot[W*H*4U];
static unsigned writes;

bool nas_fbdev_available(void) {return true;}
uint64_t nas_fbdev_length(void) {return sizeof(pixels);}
bool nas_fbdev_var_screeninfo(struct nas_fb_var_screeninfo *v) {
    if(!v)return false;
    memset(v,0,sizeof(*v));
    v->xres=W;v->yres=H;
    v->bits_per_pixel=32U;
    v->red=(struct nas_fb_bitfield){16,8,0};
    v->green=(struct nas_fb_bitfield){8,8,0};
    v->blue=(struct nas_fb_bitfield){0,8,0};
    return true;
}
bool nas_fbdev_fix_screeninfo(struct nas_fb_fix_screeninfo *f) {
    if(!f)return false;
    memset(f,0,sizeof(*f));f->line_length=W*4U;
    return true;
}
uint64_t nas_fbdev_write_at(uint64_t offset,const uint8_t *src,uint64_t len) {
    assert(offset<=sizeof(pixels) && len<=sizeof(pixels)-offset);
    assert(src || !len);
    memcpy(pixels+offset,src,(size_t)len);
    writes++;
    return len;
}

static unsigned colored_pixels(void) {
    unsigned n=0;
    for(unsigned p=0;p<sizeof(pixels);p+=4)
        if(pixels[p] || pixels[p+1] || pixels[p+2])n++;
    return n;
}
static void write_str(const char *s) {
    size_t n=strlen(s);
    assert(nas_fbcon_write((const uint8_t *)s,n)==n);
}

int main(void) {
    memset(pixels,0xff,sizeof(pixels));
    nas_fbcon_activate(true);
    write_str("Alpine login: ");
    assert(colored_pixels()>0 && colored_pixels()<W*H/2U);
    memcpy(snapshot,pixels,sizeof(pixels));
    unsigned at=writes;
    nas_fbcon_set_graphics(true);
    write_str("\033[2J\033[Hhidden while graphic owns scanout\r\n");
    assert(writes==at && memcmp(snapshot,pixels,sizeof(pixels))==0);
    nas_fbcon_set_graphics(false);
    assert(writes>at && memcmp(snapshot,pixels,sizeof(pixels))!=0);
    at=writes;
    write_str("\033[31mred\033[0m \033[2J\033[HOpenRC\r\n");
    assert(writes>at);
    for(unsigned i=0;i<50;i++)write_str("line\r\n");
    nas_fbcon_activate(false);
    at=writes;
    assert((int64_t)nas_fbcon_write((const uint8_t *)"no released VT",14)<0);
    assert(writes==at);
    puts("[M203 FBCON HOST PASS] glyphs, ANSI, scroll, graphics exclusion, repaint, release");
    return 0;
}
