/* Focused display class contract test.
 * Test the real fbdev/display implementation, not copied mock logic. */
#include "../kernel/drivers/display/fbdev.h"
#include "../kernel/drivers/display/display.h"
#include "../kernel/errno.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t pixels[48];
static unsigned open_files;
static unsigned open_pin_count(void) { return open_files; }
static uint64_t read_pixels(struct nas_display_device *dev,uint64_t off,
                            uint8_t *dst,uint64_t n) {
    memcpy(dst,(uint8_t *)dev->backend+off,(size_t)n);
    return n;
}
static uint64_t write_pixels(struct nas_display_device *dev,uint64_t off,
                             const uint8_t *src,uint64_t n) {
    memcpy((uint8_t *)dev->backend+off,src,(size_t)n);
    return n;
}
static int map_pixels(struct nas_display_device *dev,uint64_t off,uint64_t *pa) {
    (void)dev;
    *pa=UINT64_C(0x2000)+off;
    return 0;
}
static const struct nas_display_ops mock_ops={
    .read_at=read_pixels,.write_at=write_pixels,.mmap_page=map_pixels
};
static struct nas_display_mode mode(void) {
    return (struct nas_display_mode){
        .width=4,.height=3,.pitch=16,.bpp=32,
        .red_size=8,.red_shift=16,.green_size=8,.green_shift=8,
        .blue_size=8,.blue_shift=0
    };
}
int main(void) {
    struct nas_fb_boot_info boot={
        .address=pixels,.width=4,.height=3,.pitch=16,.bpp=32,
        .memory_model=1,.red_size=8,.red_shift=16,
        .green_size=8,.green_shift=8,.blue_size=8,.blue_shift=0
    };
    assert(!nas_fbdev_available());
    assert(!nas_fbdev_boot_init(NULL));
    assert(nas_fbdev_boot_init(&boot));
    assert(nas_fbdev_available() && nas_fbdev_length()==sizeof(pixels));
    struct nas_fb_var_screeninfo var;
    struct nas_fb_fix_screeninfo fix;
    assert(nas_fbdev_var_screeninfo(&var));
    assert(nas_fbdev_fix_screeninfo(&fix));
    assert(var.xres==4 && var.yres==3 && var.bits_per_pixel==32);
    assert(fix.smem_len==sizeof(pixels) && fix.line_length==16);
    assert(fix.smem_start==0 && fix.mmio_start==0);
    assert(nas_fbdev_put_var_screeninfo(&var)==0);
    assert(nas_fbdev_pan_display(&var)==0);
    var.xres=8;
    assert(nas_fbdev_put_var_screeninfo(&var)==-EINVAL);
    assert(nas_fbdev_pan_display(&var)==-EINVAL);
    uint8_t src[2]={0x5a,0x33}, dst[2]={0,0};
    assert(nas_fbdev_write_at(47,src,2)==1 && pixels[47]==0x5a);
    assert(nas_fbdev_read_at(47,dst,2)==1 && dst[0]==0x5a);
    assert(nas_display_present()==0);
    struct nas_vm_device *pin=NULL;
    assert(nas_display_mmap_pin(&pin)==-EOPNOTSUPP && pin==NULL);

    struct nas_display_device device={
        .name="mock-dma",.backend=pixels,.mode=mode(),.ops=&mock_ops
    };
    struct nas_display_device invalid=device;
    invalid.mode.width=0;
    assert(!nas_display_boot_takeover(&invalid));
    assert(nas_display_boot_takeover(&device));
    assert(!nas_display_boot_takeover(&device));
    assert(nas_display_primary()==&device);
    assert(nas_display_mmap_pin(&pin)==0 && pin==&device.vm_mapping);
    uint64_t pa=0;
    assert(pin->page(pin->context,0,&pa)==0 && pa==UINT64_C(0x2000));
    assert(pin->page(pin->context,4096,&pa)==-EINVAL);
    assert(pin->get(pin->context) && nas_display_mmap_pins(&device)==2);
    assert(nas_display_set_open_pins_callback(open_pin_count)==0);
    nas_display_lock_primary();
    open_files=1;
    assert(nas_display_unregister_primary(&device)==-EBUSY);
    open_files=0;
    assert(nas_display_unregister_primary(&device)==-EBUSY);
    pin->put(pin->context);
    pin->put(pin->context);
    assert(nas_display_mmap_pins(&device)==0);
    assert(nas_display_unregister_primary(&device)==0);
    assert(!pin->get(pin->context));
    assert(nas_display_register_primary(&device));
    assert(!nas_display_register_primary(&device));
    assert(nas_display_primary()==&device);
    puts("PASS: display/fbdev boot, mode, takeover, IO, VMA pin, detach/rebind");
    return 0;
}
