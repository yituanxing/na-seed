/* Host-only contract tests. Build with:
 * cc -std=c11 -Wall -Wextra -Werror fbdev.c fbdev_test.c -o fbdev-test
 * ./fbdev-test boot && ./fbdev-test provider && ./fbdev-test late &&
 * ./fbdev-test detach && ./fbdev-test boot-rebind && ./fbdev-test mmap-pin
 * A fresh process is used for each scenario because the primary scanout
 * has process-global state and supports only explicit, pinned-safe rebind. */
#include "display.h"
#include "fbdev.h"
#include "../../errno.h"
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
    struct nas_fb_var_screeninfo put=var;
    assert(nas_fbdev_put_var_screeninfo(&put)==0);
    assert(put.xres==var.xres && put.xoffset==0 && put.yoffset==0);
    put=var;
    put.activate=2; /* FB_ACTIVATE_TEST validates without changing mode. */
    assert(nas_fbdev_put_var_screeninfo(&put)==0 && put.activate==0);
    put=var;
    put.activate=0x80; /* FB_ACTIVATE_FORCE with unchanged fixed mode. */
    assert(nas_fbdev_put_var_screeninfo(&put)==0);
    put=var;
    put.xres=5;
    assert(nas_fbdev_put_var_screeninfo(&put)==-EINVAL);
    put=var;
    put.xres_virtual=8;
    assert(nas_fbdev_put_var_screeninfo(&put)==-EINVAL);
    put=var;
    put.red.offset=0;
    assert(nas_fbdev_put_var_screeninfo(&put)==-EINVAL);
    put=var;
    put.pixclock=1000;
    assert(nas_fbdev_put_var_screeninfo(&put)==-EINVAL);
    put=var;
    put.reserved[3]=1;
    assert(nas_fbdev_put_var_screeninfo(&put)==-EINVAL);
    put=var;
    put.height=80; /* Non-geometry physical metadata is not a modeset. */
    assert(nas_fbdev_put_var_screeninfo(&put)==0 && put.height==UINT32_MAX);
    put=var;
    put.activate=0x10; /* Deferred vsync application is unsupported. */
    assert(nas_fbdev_put_var_screeninfo(&put)==-EINVAL);
    assert(nas_fbdev_pan_display(&var)==0);
    put=var;
    put.xoffset=1;
    assert(nas_fbdev_pan_display(&put)==-EINVAL);
    put=var;
    put.yoffset=1;
    assert(nas_fbdev_pan_display(&put)==-EINVAL);
    put=var;
    put.vmode=0x100; /* FB_VMODE_YWRAP */
    assert(nas_fbdev_pan_display(&put)==-EINVAL);
    put=var;
    put.bits_per_pixel=16;
    assert(nas_fbdev_pan_display(&put)==-EINVAL);
}

static void test_boot(void) {
    assert(!nas_fbdev_available());
    struct nas_fb_var_screeninfo var={0};
    assert(nas_fbdev_put_var_screeninfo(&var)==-ENODEV);
    assert(nas_fbdev_pan_display(&var)==-ENODEV);
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
static int mock_mmap_page(struct nas_display_device *dev,uint64_t offset,
                          uint64_t *frame_pa) {
    (void)dev;
    if(offset>=sizeof(pixels) || (offset&4095U))return -EINVAL;
    *frame_pa=UINT64_C(0x2000); /* Mock owned PMM page for class-only test. */
    return 0;
}
static const struct nas_display_ops mock_mmap_ops={
    .read_at=mock_read,.write_at=mock_write,.mmap_page=mock_mmap_page
};
static unsigned pinned_files;
static unsigned file_pins(void) {return pinned_files;}
static unsigned other_file_pins(void) {return 0;}

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

/* Only an explicit successful detach creates a vacancy across the
 * user-entry lock. No ordinary late primary registration is permitted. */
static void test_locked_late(void) {
    struct nas_display_device late={
        .name="late gpu",.backend=pixels,.mode=rgb32(),.ops=&mock_ops
    };
    nas_display_lock_primary();
    assert(!nas_display_register_primary(&late));
    assert(nas_display_unregister_primary(&late)==-ENODEV);
    assert(!nas_fbdev_available());
    assert(!nas_fbdev_length());
}

static void test_detach_rebind(void) {
    struct nas_display_device first={
        .name="first GPU",.backend=pixels,.mode=rgb32(),.ops=&mock_ops
    };
    struct nas_display_device second={
        .name="second GPU",.backend=pixels,.mode=rgb32(),.ops=&mock_ops
    };
    assert(nas_display_register_primary(&first));
    nas_display_lock_primary();
    assert(nas_display_unregister_primary(NULL)==-EINVAL);
    assert(nas_display_unregister_primary(&second)==-EINVAL);
    /* Without the registered /dev/fb0 adapter a locked primary must
     * fail closed instead of guessing that no external File is open. */
    assert(nas_display_unregister_primary(&first)==-EBUSY);
    assert(nas_display_set_open_pins_callback(NULL)==-EINVAL);
    assert(nas_display_set_open_pins_callback(file_pins)==0);
    assert(nas_display_set_open_pins_callback(file_pins)==0);
    assert(nas_display_set_open_pins_callback(other_file_pins)==-EBUSY);
    pinned_files=2;
    assert(nas_display_unregister_primary(&first)==-EBUSY);
    assert(nas_display_primary()==&first);
    assert(nas_display_length()==sizeof(pixels));
    assert(nas_fbdev_available());
    assert(!nas_display_register_primary(&second));
    pinned_files=0;
    assert(nas_display_unregister_primary(&first)==0);
    assert(nas_display_unregister_primary(&first)==-ENODEV);
    assert(!nas_fbdev_available() && nas_display_primary()==NULL);
    assert(nas_fbdev_length()==0);
    assert(nas_display_read_at(0,pixels,1)==0);
    assert(nas_display_write_at(0,pixels,1)==0);
    assert(nas_display_present()==-ENODEV);
    /* A successful detach alone permits one validated register after lock. */
    second.mode.red_shift=8;
    assert(!nas_display_register_primary(&second));
    second.mode=rgb32();
    assert(nas_display_register_primary(&second));
    assert(nas_display_primary()==&second && nas_fbdev_available());
    assert(!nas_display_register_primary(&first));
    assert(nas_display_unregister_primary(&first)==-EINVAL);
    assert(nas_display_unregister_primary(&second)==0);
    assert(nas_display_register_primary(&first));
    assert(nas_display_primary()==&first);
}

static void test_boot_rebind(void) {
    struct nas_fb_boot_info fb={
        .address=pixels,.width=4,.height=3,.pitch=16,.bpp=32,
        .memory_model=1,.red_size=8,.red_shift=16,
        .green_size=8,.green_shift=8,.blue_size=8,.blue_shift=0
    };
    struct nas_display_device gpu={
        .name="gpu takeover",.backend=pixels,.mode=rgb32(),.ops=&mock_ops
    };
    struct nas_display_device replacement={
        .name="gpu replacement",.backend=pixels,.mode=rgb32(),.ops=&mock_ops
    };
    assert(nas_fbdev_boot_init(&fb));
    assert(nas_display_boot_takeover(&gpu));
    nas_display_lock_primary();
    assert(nas_display_set_open_pins_callback(file_pins)==0);
    assert(nas_display_unregister_primary(&gpu)==0);
    assert(!nas_fbdev_available());
    assert(!nas_display_boot_takeover(&replacement));
    assert(!nas_fbdev_boot_init(&fb));
    assert(nas_display_register_primary(&replacement));
    assert(nas_display_primary()==&replacement);
    assert(!nas_display_boot_takeover(&gpu));
}

static void test_mmap_pin(void) {
    struct nas_display_device owner={
        .name="mappable PMM scanout",.backend=pixels,.mode=rgb32(),
        .ops=&mock_mmap_ops
    };
    struct nas_display_device unsupported={
        .name="unmappable scanout",.backend=pixels,.mode=rgb32(),
        .ops=&mock_ops
    };
    struct nas_vm_device *map=&owner.vm_mapping;
    assert(nas_display_mmap_pin(NULL)==-EINVAL);
    assert(nas_display_mmap_pin(&map)==-ENODEV && map==NULL);
    assert(nas_display_register_primary(&unsupported));
    assert(nas_display_mmap_pin(&map)==-EOPNOTSUPP && map==NULL);
    assert(nas_display_mmap_pins(&unsupported)==0);
    assert(nas_display_unregister_primary(&unsupported)==0);
    assert(nas_display_register_primary(&owner));
    assert(nas_display_mmap_pin(&map)==0 && map==&owner.vm_mapping);
    assert(nas_display_mmap_pins(&owner)==1);
    assert(nas_display_mmap_pins(&unsupported)==0);
    assert(map->context==&owner && map->extent_bytes==sizeof(pixels) &&
           map->get && map->put && map->page);
    uint64_t physical=0;
    assert(map->page(map->context,0,&physical)==0 && physical==0x2000);
    assert(map->page(map->context,0,NULL)==-EINVAL);
    physical=UINT64_MAX;
    assert(map->page(map->context,1,&physical)==-EINVAL && !physical);
    assert(map->page(map->context,4096,&physical)==-EINVAL);
    assert(map->get(map->context)); /* fork/split VMA takes its own owner. */
    assert(nas_display_mmap_pins(&owner)==2);
    /* Closing the /dev/fb0 fd may bring OpenFile pins to zero, but the VMAs
     * still prohibit GPU unbind, DMA release and scanout replacement. */
    assert(nas_display_set_open_pins_callback(file_pins)==0);
    nas_display_lock_primary();
    assert(nas_display_unregister_primary(&owner)==-EBUSY);
    map->put(map->context);
    assert(nas_display_mmap_pins(&owner)==1);
    assert(nas_display_unregister_primary(&owner)==-EBUSY);
    map->put(map->context);
    assert(nas_display_mmap_pins(&owner)==0);
    assert(nas_display_unregister_primary(&owner)==0);
    assert(!map->get(map->context));
    assert(map->page(map->context,0,&physical)==-ENODEV);
    assert(nas_display_register_primary(&unsupported));
}

int main(int argc,char **argv) {
    if(argc!=2)return 2;
    if(strcmp(argv[1],"boot")==0)test_boot();
    else if(strcmp(argv[1],"provider")==0)test_provider();
    else if(strcmp(argv[1],"late")==0)test_locked_late();
    else if(strcmp(argv[1],"detach")==0)test_detach_rebind();
    else if(strcmp(argv[1],"boot-rebind")==0)test_boot_rebind();
    else if(strcmp(argv[1],"mmap-pin")==0)test_mmap_pin();
    else return 2;
    puts("DISPLAY BACKEND PASS");
    return 0;
}
