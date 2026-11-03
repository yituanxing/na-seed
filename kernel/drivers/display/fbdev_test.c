/* Host-only contract tests. Build with:
 * cc -std=c11 -Wall -Wextra -Werror fbdev.c fbdev_test.c -o fbdev-test
 * ./fbdev-test boot && ./fbdev-test provider
 * A fresh process is used for each scenario because the primary scanout
 * deliberately cannot be replaced after registration. */
#include "display.h"
#include "fbdev.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t pixels[4U*3U*4U];

static struct nas_display_mode rgb32(void) {
    return (struct nas_display_mode){
        .width=4,.height=3,.pitch=16,.bpp=32,
        .red_size=8,.red_shift=16,
        .green_size=8,.green_shift=8,
        .blue_size=8,.blue_shift=0
    };
}

static void assert_mode(const char *id) {
    struct nas_fb_var_screeninfo var;
    struct nas_fb_fix_screeninfo fix;
    assert(nas_fbdev_var_screeninfo(&var));
    assert(nas_fbdev_fix_screeninfo(&fix));
    assert(var.xres==4 && var.yres==3 && var.bits_per_pixel==32);
    assert(var.red.offset==16 && var.green.offset==8 && var.blue.offset==0);
    assert(fix.line_length==16 && fix.smem_len==48);
    assert(strncmp(fix.id,id,sizeof(fix.id))==0);
    assert(fix.smem_start==0 && fix.mmio_start==0);
}

static void test_boot(void) {
    assert(!nas_fbdev_available());
    assert(nas_fbdev_length()==0);
    assert(nas_fbdev_read_at(0,pixels,1)==0);
    assert(nas_fbdev_write_at(0,pixels,1)==0);
    struct nas_fb_boot_info info={
        .address=pixels,.width=4,.height=3,.pitch=16,.bpp=32,
        .memory_model=1,.red_size=8,.red_shift=16,
        .green_size=8,.green_shift=8,.blue_size=8,.blue_shift=0
    };
    struct nas_fb_boot_info invalid=info;
    invalid.pitch=15;
    assert(!nas_fbdev_boot_init(&invalid));
    invalid=info;
    invalid.red_shift=8; /* Overlap the green component. */
    assert(!nas_fbdev_boot_init(&invalid));
    invalid=info;
    invalid.address=(volatile uint8_t *)(uintptr_t)(UINTPTR_MAX-2U);
    assert(!nas_fbdev_boot_init(&invalid));
    assert(!nas_fbdev_available());
    assert(nas_fbdev_boot_init(&info));
    assert(nas_fbdev_available());
    assert(nas_fbdev_length()==sizeof(pixels));
    assert_mode("NaSeed boot fb");
    const uint8_t pattern[]={0xa5,0x5a,0x80,0x7f};
    assert(nas_fbdev_write_at(sizeof(pixels)-2U,pattern,4)==2);
    assert(pixels[sizeof(pixels)-2U]==pattern[0]);
    assert(pixels[sizeof(pixels)-1U]==pattern[1]);
    uint8_t got[4]={0};
    assert(nas_fbdev_read_at(sizeof(pixels)-2U,got,4)==2);
    assert(got[0]==pattern[0] && got[1]==pattern[1]);
    assert(nas_fbdev_read_at(sizeof(pixels),got,4)==0);
    assert(nas_fbdev_write_at(0,NULL,1)==0);
    assert(!nas_fbdev_boot_init(NULL)); /* Boot-only firmware retry API. */
    assert(!nas_fbdev_available());
}

static uint64_t mock_read(struct nas_display_device *dev,uint64_t offset,
                          uint8_t *dst,uint64_t bytes) {
    const uint8_t *source=dev->backend;
    memcpy(dst,source+offset,(size_t)bytes);
    return bytes;
}
static uint64_t mock_write(struct nas_display_device *dev,uint64_t offset,
                           const uint8_t *src,uint64_t bytes) {
    uint8_t *dest=dev->backend;
    memcpy(dest+offset,src,(size_t)bytes);
    return bytes;
}
static const struct nas_display_ops mock_ops={
    .read_at=mock_read,.write_at=mock_write
};

static void test_provider(void) {
    struct nas_display_device other={
        .name="mock scanout",.backend=pixels,.mode=rgb32(),.ops=&mock_ops
    };
    assert(!nas_fbdev_available());
    other.mode.red_shift=8;
    assert(!nas_display_register_primary(&other));
    assert(!nas_fbdev_available());
    other.mode=rgb32();
    assert(nas_display_register_primary(&other));
    assert(nas_display_primary()==&other);
    assert(nas_fbdev_available());
    assert_mode("mock scanout");
    const uint8_t pattern[]={9,8,7};
    assert(nas_display_write_at(46,pattern,3)==2);
    uint8_t got[3]={0};
    assert(nas_fbdev_read_at(46,got,3)==2);
    assert(got[0]==9 && got[1]==8);
    assert(!nas_display_register_primary(&other));
}

int main(int argc,char **argv) {
    if(argc!=2)return 2;
    if(strcmp(argv[1],"boot")==0)test_boot();
    else if(strcmp(argv[1],"provider")==0)test_provider();
    else return 2;
    puts("DISPLAY BACKEND PASS");
    return 0;
}
