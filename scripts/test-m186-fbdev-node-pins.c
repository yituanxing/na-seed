/* M186: production /dev/fb0 class node holds one pin for each distinct
 * shared OpenFile, not one per alias. Exercise the actual persistent node
 * and DisplayDevice registry instead of a second fake framebuffer model. */
#include "../kernel/drivers/display/display.h"
#include "../kernel/drivers/display/fbdev.h"
#include "../kernel/drivers/core/chrdev.h"
#include "../kernel/errno.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t pixels[64];
static uint64_t read_at(struct nas_display_device *d,uint64_t offset,
                        uint8_t *out,uint64_t size) {
    memcpy(out,(uint8_t *)d->backend+offset,(size_t)size);
    return size;
}
static uint64_t write_at(struct nas_display_device *d,uint64_t offset,
                         const uint8_t *in,uint64_t size) {
    memcpy((uint8_t *)d->backend+offset,in,(size_t)size);
    return size;
}
static const struct nas_display_ops ops={.read_at=read_at,.write_at=write_at};
static struct nas_display_device display={
    .name="M186-vfb",.backend=pixels,.ops=&ops,
    .mode={.width=4,.height=4,.pitch=16,.bpp=32,
           .red_size=8,.red_shift=16,.green_size=8,.green_shift=8,
           .blue_size=8,.blue_shift=0}
};
static struct nas_display_device rebound={
    .name="M186-rebound-vfb",.backend=pixels,.ops=&ops,
    .mode={.width=4,.height=4,.pitch=16,.bpp=32,
           .red_size=8,.red_shift=16,.green_size=8,.green_shift=8,
           .blue_size=8,.blue_shift=0}
};
static unsigned fake_open_pins(void) { return 0; }

static void test_pins_and_rebind(void) {
    struct nas_chrdev_info info={0};
    const struct nas_chrdev_provider *provider=NULL;
    struct nas_chrdev_node *node=NULL;
    assert(nas_fbdev_chrdev_init()==0);
    assert(nas_chrdev_describe("/dev/fb0",&info,&provider,&node)==-ENOENT);
    assert(nas_display_register_primary(&display));
    assert(nas_chrdev_describe("/dev/fb0",&info,&provider,&node)==0);
    assert(!provider && node && info.rdev==(29U<<8));
    nas_display_lock_primary();
    /* Simulate two independent shared open descriptions. File aliases
     * (dup/fork/SCM_RIGHTS) share one pin and are not counted again. */
    assert(nas_chrdev_node_pin(node)==0);
    assert(nas_display_unregister_primary(&display)==-EBUSY);
    assert(nas_chrdev_node_pin(node)==0);
    assert(nas_display_unregister_primary(&display)==-EBUSY);
    assert(nas_fbdev_available());
    assert(nas_chrdev_node_unregister(node)==-EBUSY);
    nas_chrdev_node_put(node);
    assert(nas_display_unregister_primary(&display)==-EBUSY);
    nas_chrdev_node_put(node);
    assert(nas_fbdev_available());
    assert(nas_display_unregister_primary(&display)==0);
    assert(!nas_fbdev_available() && !nas_fbdev_length());
    assert(nas_display_primary()==NULL);
    assert(nas_display_present()==-ENODEV);
    uint8_t x=0;
    assert(nas_fbdev_read_at(0,&x,1)==0);
    assert(nas_chrdev_describe("/dev/fb0",&info,&provider,&node)==-ENOENT);
    /* One explicit vacancy may be filled even after userspace lock;
     * initial late registration without such a vacancy stays forbidden. */
    assert(nas_display_register_primary(&rebound));
    assert(nas_display_primary()==&rebound);
    assert(nas_chrdev_describe("/dev/fb0",&info,&provider,&node)==0);
    assert(nas_display_boot_takeover(&display)==false);
    assert(nas_display_set_open_pins_callback(fake_open_pins)==-EBUSY);
}
static void test_injection_collision_rollback(void) {
    struct nas_chrdev_info info={0};
    const struct nas_chrdev_provider *provider=NULL;
    struct nas_chrdev_node *node=NULL;
    assert(nas_display_register_primary(&display));
    assert(nas_display_set_open_pins_callback(fake_open_pins)==0);
    assert(nas_fbdev_chrdev_init()==-EBUSY);
    /* Roll back the just-published devfs node on callback registration
     * error, even though a display backend is active. */
    assert(nas_chrdev_describe("/dev/fb0",&info,&provider,&node)==-ENOENT);
    assert(nas_chrdev_node_at(0)==NULL);
}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    if(!strcmp(argv[1],"pins"))test_pins_and_rebind();
    else if(!strcmp(argv[1],"collision"))test_injection_collision_rollback();
    else return 2;
    puts("[M186 FBDEV NODE PINS PASS] class node holds shared OpenFile pins, protects teardown, supports rebind, rolls back callback collision");
    return 0;
}
