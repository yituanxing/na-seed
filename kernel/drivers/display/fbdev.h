#ifndef NASEED_DISPLAY_FBDEV_H
#define NASEED_DISPLAY_FBDEV_H
#include <stdbool.h>
#include <stdint.h>

/* The first DisplayDevice backend. Bootloader-provided and kernel-mapped;
 * the bootloader owns the pages and they are never mapped to userspace. */
struct nas_fb_boot_info {
    volatile uint8_t *address;
    uint64_t width, height, pitch;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_size, red_shift;
    uint8_t green_size, green_shift;
    uint8_t blue_size, blue_shift;
};

/* Linux LP64 fbdev wire structs, common to x86_64, riscv64 and aarch64.
 * Keep the fixed-screen-info memory BAR address zero: there is no mmap ABI. */
struct nas_fb_bitfield {
    uint32_t offset, length, msb_right;
};
struct nas_fb_var_screeninfo {
    uint32_t xres, yres, xres_virtual, yres_virtual, xoffset, yoffset;
    uint32_t bits_per_pixel, grayscale;
    struct nas_fb_bitfield red, green, blue, transp;
    uint32_t nonstd, activate, height, width, accel_flags;
    uint32_t pixclock, left_margin, right_margin, upper_margin, lower_margin;
    uint32_t hsync_len, vsync_len, sync, vmode, rotate, colorspace;
    uint32_t reserved[4];
};
struct nas_fb_fix_screeninfo {
    char id[16];
    uint64_t smem_start;
    uint32_t smem_len, type, type_aux, visual;
    uint16_t xpanstep, ypanstep, ywrapstep;
    uint32_t line_length;
    uint64_t mmio_start;
    uint32_t mmio_len, accel;
    uint16_t capabilities, reserved[2];
};
_Static_assert(sizeof(struct nas_fb_bitfield)==12, "Linux fb_bitfield ABI");
_Static_assert(sizeof(struct nas_fb_var_screeninfo)==160, "Linux fb_var_screeninfo ABI");
_Static_assert(sizeof(struct nas_fb_fix_screeninfo)==80, "Linux LP64 fb_fix_screeninfo ABI");

#define NAS_FBIOGET_VSCREENINFO 0x4600U
#define NAS_FBIOGET_FSCREENINFO 0x4602U

bool nas_fbdev_boot_init(const struct nas_fb_boot_info *);
bool nas_fbdev_available(void);
/* Publish the active DisplayDevice as an ordinary character endpoint. */
int nas_fbdev_chrdev_init(void);
uint64_t nas_fbdev_length(void);
uint64_t nas_fbdev_read_at(uint64_t offset,uint8_t *dst,uint64_t bytes);
uint64_t nas_fbdev_write_at(uint64_t offset,const uint8_t *src,uint64_t bytes);
bool nas_fbdev_var_screeninfo(struct nas_fb_var_screeninfo *);
bool nas_fbdev_fix_screeninfo(struct nas_fb_fix_screeninfo *);
#endif
