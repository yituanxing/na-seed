#include "fbdev.h"
#include "display.h"
#include "../../errno.h"
#include <stddef.h>
#include <stdint.h>

/* The display class, the Linux fbdev personality and the first physical
 * backend share this object file for now so every supported kernel profile
 * links the same code. Their state/operations remain separately owned. */
static struct nas_display_device *primary_display;
static uint64_t primary_bytes;
static bool primary_locked;
static bool boot_taken_over;
/* Do not permit arbitrary late discovery after lock. A successful explicit
 * detach, which has checked all /dev/fb0 OpenFile pins, authorizes the next
 * primary registration even if the kernel already entered userspace. */
static bool primary_vacant;
static unsigned (*primary_open_pins)(void);
/* VMAs outlive the originating shared OpenFile and can be cloned on fork.
 * They pin the exact display owner and its DMA pages independently of fds. */
static unsigned primary_mmap_pins;

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

static bool valid_device(const struct nas_display_device *device,
                         uint64_t *bytes) {
    return device && device->name && device->name[0] && device->ops &&
       device->ops->read_at && device->ops->write_at &&
       mode_valid(&device->mode,bytes);
}

static bool display_vm_get(void *context) {
    const struct nas_display_device *device=context;
    if(!device || device!=primary_display || !device->ops ||
       !device->ops->mmap_page || primary_mmap_pins==UINT32_MAX)return false;
    ++primary_mmap_pins;
    return true;
}
static void display_vm_put(void *context) {
    if(context!=primary_display || !primary_mmap_pins)__builtin_trap();
    --primary_mmap_pins;
}
static int display_vm_page(void *context,uint64_t offset,uint64_t *pa) {
    struct nas_display_device *device=context;
    if(!pa)return -EINVAL;
    *pa=0;
    if(!device || device!=primary_display)return -ENODEV;
    if(!device->ops || !device->ops->mmap_page)return -EOPNOTSUPP;
    if((offset&UINT64_C(4095)) || offset>=primary_bytes)return -EINVAL;
    int rc=device->ops->mmap_page(device,offset,pa);
    if(rc) {*pa=0;return rc;}
    if(!*pa || (*pa&UINT64_C(4095))) {*pa=0;return -EIO;}
    return 0;
}
static void display_vm_init(struct nas_display_device *device) {
    device->vm_mapping=(struct nas_vm_device){
        .context=device,.extent_bytes=primary_bytes,.get=display_vm_get,
        .put=display_vm_put,.page=display_vm_page
    };
}
int nas_display_mmap_pin(struct nas_vm_device **out) {
    if(!out)return -EINVAL;
    *out=NULL;
    if(!primary_display)return -ENODEV;
    if(!primary_display->ops || !primary_display->ops->mmap_page)
        return -EOPNOTSUPP;
    /* Fail unsupported DMA/MMIO backing at mmap(2), not at the first user
     * page fault. The backend must already be able to serve page zero. */
    uint64_t first_frame=0;
    int rc=display_vm_page(primary_display,0,&first_frame);
    if(rc)return rc;
    if(!primary_display->vm_mapping.get(primary_display))return -EBUSY;
    *out=&primary_display->vm_mapping;
    return 0;
}
unsigned nas_display_mmap_pins(const struct nas_display_device *device) {
    return device && primary_display==device ? primary_mmap_pins : 0;
}

bool nas_display_register_primary(struct nas_display_device *device) {
    uint64_t bytes=0;
    if(primary_display || (primary_locked && !primary_vacant) ||
       !valid_device(device,&bytes))
        return false;
    primary_display=device;
    primary_bytes=bytes;
    primary_mmap_pins=0;
    display_vm_init(device);
    primary_vacant=false;
    return true;
}
bool nas_display_boot_takeover(struct nas_display_device *device) {
    uint64_t bytes=0;
    if(primary_locked || boot_taken_over || primary_display!=&boot_display ||
       device==&boot_display || !valid_device(device,&bytes))return false;
    /* The old boot framebuffer is firmware-owned and stays mapped, but no
     * displayed File exists yet. Switch identity and mode atomically before
     * init exposes the first process. No runtime framebuffer replacement. */
    primary_display=device;
    primary_bytes=bytes;
    primary_mmap_pins=0;
    display_vm_init(device);
    boot_taken_over=true;
    primary_vacant=false;
    return true;
}
int nas_display_set_open_pins_callback(unsigned (*query)(void)) {
    if(!query)return -EINVAL;
    if(primary_open_pins && primary_open_pins!=query)return -EBUSY;
    primary_open_pins=query;
    return 0;
}
int nas_display_unregister_primary(struct nas_display_device *device) {
    if(!device)return -EINVAL;
    if(!primary_display)return -ENODEV;
    if(primary_display!=device)return -EINVAL;
    /* After user entry the class must know how to count published /dev/fb0
     * OpenFile pins. Never allow a missing adapter/query to masquerade as
     * zero live readers. Bootstrap failures can still retire pre-lock. */
    if(primary_locked && !primary_open_pins)return -EBUSY;
    if(primary_open_pins && primary_open_pins())return -EBUSY;
    if(primary_mmap_pins)return -EBUSY;
    primary_display=NULL;
    primary_bytes=0;
    primary_vacant=true;
    return 0;
}
void nas_display_lock_primary(void) {primary_locked=true;}
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
int nas_display_present(void) {
    if(!primary_display)return -ENODEV;
    return primary_display->ops->present ?
        primary_display->ops->present(primary_display) : 0;
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
static const struct nas_display_ops boot_ops={
    .read_at=boot_read,.write_at=boot_write
};

bool nas_fbdev_boot_init(const struct nas_fb_boot_info *src) {
    /* Firmware initialization is a bootstrap-only operation, before any
     * user File can refer to /dev/fb0. Preserve the historical ability to
     * reject/replace a bad Limine candidate while never displacing a
     * separately registered DisplayDevice backend. Explicit post-bootstrap
     * removal/rebind instead uses the DisplayDevice owner API above. */
    if(primary_locked || boot_taken_over ||
       (primary_display && primary_display!=&boot_display))return false;
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
/* fbdev's mode-setting ABI is independent of the scanout's buffer writes.
 * Until a DisplayDevice explicitly provides modesetting, make identical-mode
 * requests legal without claiming to implement a hardware mode transition. */
static bool fb_bitfield_matches(struct nas_fb_bitfield requested,
                                struct nas_fb_bitfield current) {
    return requested.offset==current.offset &&
           requested.length==current.length &&
           requested.msb_right==current.msb_right;
}
static bool fb_fixed_geometry_matches(const struct nas_fb_var_screeninfo *requested,
                                      const struct nas_fb_var_screeninfo *current) {
    return requested->xres==current->xres &&
           requested->yres==current->yres &&
           requested->xres_virtual==current->xres_virtual &&
           requested->yres_virtual==current->yres_virtual &&
           requested->bits_per_pixel==current->bits_per_pixel &&
           fb_bitfield_matches(requested->red,current->red) &&
           fb_bitfield_matches(requested->green,current->green) &&
           fb_bitfield_matches(requested->blue,current->blue) &&
           fb_bitfield_matches(requested->transp,current->transp);
}
int nas_fbdev_put_var_screeninfo(struct nas_fb_var_screeninfo *requested) {
    if(!requested)return -EINVAL;
    struct nas_fb_var_screeninfo current;
    if(!nas_fbdev_var_screeninfo(&current))return -ENODEV;
    /* FB_ACTIVATE_NOW (0) and FB_ACTIVATE_TEST (2) are synchronous; the
     * FORCE bit can request re-application of an already active mode. */
    if((requested->activate & ~UINT32_C(0x82))!=0U ||
       !fb_fixed_geometry_matches(requested,&current) ||
       requested->xoffset || requested->yoffset ||
       requested->grayscale || requested->nonstd ||
       requested->vmode || requested->rotate || requested->colorspace ||
       requested->accel_flags!=current.accel_flags ||
       requested->pixclock!=current.pixclock ||
       requested->left_margin!=current.left_margin ||
       requested->right_margin!=current.right_margin ||
       requested->upper_margin!=current.upper_margin ||
       requested->lower_margin!=current.lower_margin ||
       requested->hsync_len!=current.hsync_len ||
       requested->vsync_len!=current.vsync_len ||
       requested->sync!=current.sync)return -EINVAL;
    for(unsigned i=0;i<4U;i++)
        if(requested->reserved[i]!=current.reserved[i])return -EINVAL;
    /* Linux fbdev's FBIOPUT_VSCREENINFO is read/write. Return the canonical
     * fixed mode, including the actual RGB format and physical dimensions. */
    *requested=current;
    return 0;
}
int nas_fbdev_pan_display(const struct nas_fb_var_screeninfo *requested) {
    if(!requested)return -EINVAL;
    struct nas_fb_var_screeninfo current;
    if(!nas_fbdev_var_screeninfo(&current))return -ENODEV;
    /* xres_virtual == xres and yres_virtual == yres; no second page exists
     * to pan to, and the backend cannot offer YWRAP. */
    return !fb_fixed_geometry_matches(requested,&current) ||
           requested->xoffset || requested->yoffset || requested->vmode ?
           -EINVAL : 0;
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
    /* A zero smem_start never exposes DMA's physical address through ioctl.
     * Actual mmap eligibility is decided independently by a pinned display
     * provider and a PMM-backed page callback, not by these UAPI fields. */
    *out=info;
    return true;
}
