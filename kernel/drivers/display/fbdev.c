#include "fbdev.h"
#include <stddef.h>
#include <stdint.h>

/* A single optional firmware display, not a newly probed GPU or a PMM RAM
 * allocation. Limine's framebuffer pointer is already a kernel VA and stays
 * valid while the inherited kernel half of the page tables remains mapped.
 * The first userspace contract is bounded read/write + mode query, NO mmap. */
static struct nas_fb_boot_info boot_surface;
static uint64_t surface_bytes;

static bool valid_component(uint8_t size,uint8_t shift,uint16_t bpp,
                            uint64_t *mask) {
    if(!size || size>bpp || shift>bpp-size)return false;
    *mask=((UINT64_C(1)<<size)-1U)<<shift;
    return true;
}
bool nas_fbdev_boot_init(const struct nas_fb_boot_info *src) {
    boot_surface=(struct nas_fb_boot_info){0};
    surface_bytes=0;
    if(!src || !src->address || src->memory_model!=1U ||
       !src->width || !src->height || !src->pitch ||
       src->width>UINT32_MAX || src->height>UINT32_MAX ||
       src->pitch>UINT32_MAX ||
       (src->bpp!=16U && src->bpp!=24U && src->bpp!=32U))return false;
    const uint64_t pixel_bytes=src->bpp/8U;
    if(src->width>src->pitch/pixel_bytes ||
       src->height>UINT64_MAX/src->pitch)return false;
    const uint64_t length=src->height*src->pitch;
    /* LP64 fb_fix_screeninfo.smem_len is u32. A modest cap also rejects
     * corrupted firmware dimensions before exposing any memory reads. */
    if(!length || length>UINT32_MAX || length>UINT64_C(256)*1024*1024 ||
       (uintptr_t)src->address>UINTPTR_MAX-length)return false;
    uint64_t red=0,green=0,blue=0;
    if(!valid_component(src->red_size,src->red_shift,src->bpp,&red) ||
       !valid_component(src->green_size,src->green_shift,src->bpp,&green) ||
       !valid_component(src->blue_size,src->blue_shift,src->bpp,&blue) ||
       (red&green) || (red&blue) || (green&blue))return false;
    boot_surface=*src;
    surface_bytes=length;
    return true;
}
bool nas_fbdev_available(void) {return surface_bytes!=0;}
uint64_t nas_fbdev_length(void) {return surface_bytes;}
uint64_t nas_fbdev_read_at(uint64_t offset,uint8_t *dst,uint64_t bytes) {
    if(!nas_fbdev_available() || (bytes && !dst))return 0;
    if(offset>=surface_bytes)return 0;
    if(bytes>surface_bytes-offset)bytes=surface_bytes-offset;
    const volatile uint8_t *from=boot_surface.address+offset;
    for(uint64_t i=0;i<bytes;i++)dst[i]=from[i];
    return bytes;
}
uint64_t nas_fbdev_write_at(uint64_t offset,const uint8_t *src,uint64_t bytes) {
    if(!nas_fbdev_available() || (bytes && !src))return 0;
    if(offset>=surface_bytes)return 0;
    if(bytes>surface_bytes-offset)bytes=surface_bytes-offset;
    volatile uint8_t *to=boot_surface.address+offset;
    for(uint64_t i=0;i<bytes;i++)to[i]=src[i];
    return bytes;
}
bool nas_fbdev_var_screeninfo(struct nas_fb_var_screeninfo *out) {
    if(!out || !nas_fbdev_available())return false;
    struct nas_fb_var_screeninfo info={0};
    info.xres=info.xres_virtual=(uint32_t)boot_surface.width;
    info.yres=info.yres_virtual=(uint32_t)boot_surface.height;
    info.bits_per_pixel=boot_surface.bpp;
    info.red=(struct nas_fb_bitfield){boot_surface.red_shift,boot_surface.red_size,0};
    info.green=(struct nas_fb_bitfield){boot_surface.green_shift,boot_surface.green_size,0};
    info.blue=(struct nas_fb_bitfield){boot_surface.blue_shift,boot_surface.blue_size,0};
    info.height=info.width=UINT32_MAX; /* physical millimetres unavailable */
    *out=info;
    return true;
}
bool nas_fbdev_fix_screeninfo(struct nas_fb_fix_screeninfo *out) {
    if(!out || !nas_fbdev_available())return false;
    struct nas_fb_fix_screeninfo info={0};
    const char name[]="NaSeed boot fb";
    for(unsigned i=0;i<sizeof(name);i++)info.id[i]=name[i];
    info.smem_len=(uint32_t)surface_bytes;
    info.line_length=(uint32_t)boot_surface.pitch;
    info.type=0; /* FB_TYPE_PACKED_PIXELS */
    info.visual=2; /* FB_VISUAL_TRUECOLOR */
    /* 0 addresses are intentional: framebuffer physical mapping and ioctl
     * metadata must never become an ownership bypass or an mmap promise. */
    *out=info;
    return true;
}
