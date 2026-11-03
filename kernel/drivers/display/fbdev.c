#include "fbdev.h"
#include "display.h"
#include <stddef.h>
#include <stdint.h>

/* The display class, the Linux fbdev personality and the first physical
 * backend share this object file for now so every supported kernel profile
 * links the same code. Their state/operations remain separately owned. */
static struct nas_display_device *primary_display;
static uint64_t primary_bytes;

/* A Limine-provided framebuffer is an ordinary *backend*, not the display
 * interface itself. These pages belong to firmware and are already mapped
 * into the kernel VA; no PMM allocation, DMA ownership or user mmap. */
static struct nas_fb_boot_info boot_surface;
static struct nas_display_device boot_display;

static bool valid_component(uint8_t size,uint8_t shift,uint16_t bpp,
                            uint64_t *mask) {
    if(!size || size>bpp || shift>bpp-size)return false;
    *mask=((UINT64_C(1)<<size)-1U)<<shift;
    return true;
}
static bool mode_valid(const struct nas_display_mode *mode,uint64_t *bytes) {
    if(!mode || !mode->width || !mode->height || !mode->pitch ||
       (mode->bpp!=16U && mode->bpp!=24U && mode->bpp!=32U))return false;
    const uint64_t pixel_bytes=mode->bpp/8U;
    if(mode->width>mode->pitch/pixel_bytes)return false;
    const uint64_t length=(uint64_t)mode->height*mode->pitch;
    /* Linux fb_fix_screeninfo.smem_len is u32. Reject implausibly large
     * surfaces rather than publishing a truncated or overflowing length. */
    if(!length || length>UINT32_MAX || length>UINT64_C(256)*1024*1024)return false;
    uint64_t red=0,green=0,blue=0;
    if(!valid_component(mode->red_size,mode->red_shift,mode->bpp,&red) ||
       !valid_component(mode->green_size,mode->green_shift,mode->bpp,&green) ||
       !valid_component(mode->blue_size,mode->blue_shift,mode->bpp,&blue) ||
       (red&green) || (red&blue) || (green&blue))return false;
    *bytes=length;
    return true;
}

bool nas_display_register_primary(struct nas_display_device *device) {
    uint64_t bytes=0;
    if(primary_display || !device || !device->name || !device->ops ||
       !device->ops->read_at || !device->ops->write_at ||
       !mode_valid(&device->mode,&bytes))return false;
    primary_display=device;
    primary_bytes=bytes;
    return true;
}
const struct nas_display_device *nas_display_primary(void) {
    return primary_display;
}
uint64_t nas_display_length(void) {return primary_bytes;}
uint64_t nas_display_read_at(uint64_t offset,uint8_t *dst,uint64_t bytes) {
    if(!primary_display || (bytes && !dst) || offset>=primary_bytes)return 0;
    if(bytes>primary_bytes-offset)bytes=primary_bytes-offset;
    uint64_t done=primary_display->ops->read_at(primary_display,offset,dst,bytes);
    return done<=bytes ? done : 0;
}
uint64_t nas_display_write_at(uint64_t offset,const uint8_t *src,uint64_t bytes) {
    if(!primary_display || (bytes && !src) || offset>=primary_bytes)return 0;
    if(bytes>primary_bytes-offset)bytes=primary_bytes-offset;
    uint64_t done=primary_display->ops->write_at(primary_display,offset,src,bytes);
    return done<=bytes ? done : 0;
}

static uint64_t boot_read(struct nas_display_device *device,uint64_t offset,
                          uint8_t *dst,uint64_t bytes) {
    const struct nas_fb_boot_info *boot=device->backend;
    const volatile uint8_t *from=boot->address+offset;
    for(uint64_t i=0;i<bytes;i++)dst[i]=from[i];
    return bytes;
}
static uint64_t boot_write(struct nas_display_device *device,uint64_t offset,
                           const uint8_t *src,uint64_t bytes) {
    const struct nas_fb_boot_info *boot=device->backend;
    volatile uint8_t *to=boot->address+offset;
    for(uint64_t i=0;i<bytes;i++)to[i]=src[i];
    return bytes;
}
static const struct nas_display_ops boot_ops={boot_read,boot_write};

bool nas_fbdev_boot_init(const struct nas_fb_boot_info *src) {
    /* Firmware initialization is a bootstrap-only operation, before any
     * user File can refer to /dev/fb0. Preserve the historical ability to
     * reject/replace a bad Limine candidate while never displacing a
     * separately registered DisplayDevice backend. Runtime hot-unplug is
     * intentionally not an operation exposed by this display class. */
    if(primary_display && primary_display!=&boot_display)return false;
    if(primary_display==&boot_display) {
        primary_display=NULL;
        primary_bytes=0;
    }
    boot_surface=(struct nas_fb_boot_info){0};
    boot_display=(struct nas_display_device){0};
    if(!src || !src->address || src->memory_model!=1U ||
       src->width>UINT32_MAX || src->height>UINT32_MAX ||
       src->pitch>UINT32_MAX)return false;
    const struct nas_display_mode mode={
        .width=(uint32_t)src->width,.height=(uint32_t)src->height,
        .pitch=(uint32_t)src->pitch,.bpp=src->bpp,
        .red_size=src->red_size,.red_shift=src->red_shift,
        .green_size=src->green_size,.green_shift=src->green_shift,
        .blue_size=src->blue_size,.blue_shift=src->blue_shift
    };
    uint64_t length=0;
    if(!mode_valid(&mode,&length) ||
       (uintptr_t)src->address>UINTPTR_MAX-length)return false;
    boot_surface=*src;
    boot_display=(struct nas_display_device){
        .name="NaSeed boot fb",.backend=&boot_surface,.mode=mode,.ops=&boot_ops
    };
    return nas_display_register_primary(&boot_display);
}

/* Linux fbdev compatibility is a view of whichever scanout backend the
 * display class published; neither ioctl nor OpenFile sees Limine. */
bool nas_fbdev_available(void) {return nas_display_primary()!=NULL;}
uint64_t nas_fbdev_length(void) {return nas_display_length();}
uint64_t nas_fbdev_read_at(uint64_t offset,uint8_t *dst,uint64_t bytes) {
    return nas_display_read_at(offset,dst,bytes);
}
uint64_t nas_fbdev_write_at(uint64_t offset,const uint8_t *src,uint64_t bytes) {
    return nas_display_write_at(offset,src,bytes);
}
bool nas_fbdev_var_screeninfo(struct nas_fb_var_screeninfo *out) {
    if(!out || !nas_fbdev_available())return false;
    const struct nas_display_mode *mode=&primary_display->mode;
    struct nas_fb_var_screeninfo info={0};
    info.xres=info.xres_virtual=mode->width;
    info.yres=info.yres_virtual=mode->height;
    info.bits_per_pixel=mode->bpp;
    info.red=(struct nas_fb_bitfield){mode->red_shift,mode->red_size,0};
    info.green=(struct nas_fb_bitfield){mode->green_shift,mode->green_size,0};
    info.blue=(struct nas_fb_bitfield){mode->blue_shift,mode->blue_size,0};
    info.height=info.width=UINT32_MAX; /* physical millimetres unavailable */
    *out=info;
    return true;
}
bool nas_fbdev_fix_screeninfo(struct nas_fb_fix_screeninfo *out) {
    if(!out || !nas_fbdev_available())return false;
    struct nas_fb_fix_screeninfo info={0};
    const char *name=primary_display->name;
    for(unsigned i=0;i<sizeof(info.id)-1U && name[i];i++)info.id[i]=name[i];
    info.smem_len=(uint32_t)primary_bytes;
    info.line_length=primary_display->mode.pitch;
    info.type=0; /* FB_TYPE_PACKED_PIXELS */
    info.visual=2; /* FB_VISUAL_TRUECOLOR */
    /* 0 addresses are intentional: framebuffer physical mapping and ioctl
     * metadata must never become an ownership bypass or an mmap promise. */
    *out=info;
    return true;
}
