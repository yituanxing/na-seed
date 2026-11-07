/* Host-side unit oracle: official Linux fbdev wire ABI plus a fake Limine
 * video surface. This never claims a graphical QEMU screen was observed. */
#include "../kernel/drivers/display/fbdev.h"
#include <assert.h>
#include <linux/fb.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(sizeof(struct nas_fb_fix_screeninfo)==sizeof(struct fb_fix_screeninfo),
               "Linux fb_fix_screeninfo size mismatch");
_Static_assert(sizeof(struct nas_fb_var_screeninfo)==sizeof(struct fb_var_screeninfo),
               "Linux fb_var_screeninfo size mismatch");
_Static_assert(offsetof(struct nas_fb_fix_screeninfo,line_length)==
               offsetof(struct fb_fix_screeninfo,line_length),
               "Linux fb_fix_screeninfo field mismatch");
_Static_assert(offsetof(struct nas_fb_var_screeninfo,red)==
               offsetof(struct fb_var_screeninfo,red),
               "Linux fb_var_screeninfo field mismatch");
_Static_assert(offsetof(struct nas_fb_var_screeninfo,reserved)==
               offsetof(struct fb_var_screeninfo,reserved),
               "Linux fb_var_screeninfo tail mismatch");

int main(void) {
    uint8_t raw[32];
    memset(raw,0xa5,sizeof(raw));
    struct nas_fb_boot_info info={
        .address=raw,.width=2,.height=2,.pitch=12,.bpp=32,
        .memory_model=1,.red_size=8,.red_shift=16,
        .green_size=8,.green_shift=8,.blue_size=8,.blue_shift=0
    };
    assert(!nas_fbdev_available());
    assert(!nas_fbdev_boot_init(NULL));
    assert(!nas_fbdev_available());
    info.pitch=7; /* 2 * 4 bytes do not fit a 7-byte row. */
    assert(!nas_fbdev_boot_init(&info));
    info.pitch=12;
    info.green_shift=16; /* Overlapping masks must be rejected. */
    assert(!nas_fbdev_boot_init(&info));
    info.green_shift=8;
    info.height=UINT64_MAX; /* Pitch * height must not wrap. */
    assert(!nas_fbdev_boot_init(&info));
    info.height=2;
    assert(nas_fbdev_boot_init(&info));
    assert(nas_fbdev_available() && nas_fbdev_length()==24);
    uint8_t rgb[5]={1,2,3,4,5},roundtrip[5]={0};
    assert(nas_fbdev_write_at(21,rgb,sizeof(rgb))==3);
    assert(nas_fbdev_read_at(21,roundtrip,sizeof(roundtrip))==3);
    assert(!memcmp(rgb,roundtrip,3));
    assert(raw[20]==0xa5 && raw[21]==1 && raw[22]==2 && raw[23]==3 &&
           raw[24]==0xa5);
    assert(nas_fbdev_read_at(24,roundtrip,2)==0);
    assert(nas_fbdev_write_at(24,rgb,2)==0);

    struct nas_fb_var_screeninfo var;
    struct nas_fb_fix_screeninfo fix;
    assert(nas_fbdev_var_screeninfo(&var));
    assert(nas_fbdev_fix_screeninfo(&fix));
    assert(var.xres==2 && var.yres==2 && var.xres_virtual==2 &&
           var.yres_virtual==2 && var.bits_per_pixel==32 &&
           var.red.offset==16 && var.green.offset==8 &&
           var.blue.offset==0);
    assert(fix.line_length==12 && fix.smem_len==24 &&
           fix.smem_start==0 && fix.mmio_start==0 &&
           fix.type==FB_TYPE_PACKED_PIXELS &&
           fix.visual==FB_VISUAL_TRUECOLOR);
    assert(!nas_fbdev_boot_init(NULL));
    assert(!nas_fbdev_available());
    return 0;
}
