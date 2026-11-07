/* M177: REAL class-node registration + one boot-stage DisplayDevice handoff.
 * This host test uses production display and chrdev implementation, not
 * distinct simulated registries. Invoke with 'handoff' and 'locked'. */
#include "../kernel/drivers/display/display.h"
#include "../kernel/drivers/display/fbdev.h"
#include "../kernel/drivers/core/chrdev.h"
#include "../kernel/errno.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t boot_pixels[64],gpu_pixels[64];
static unsigned presentations;
static uint64_t gpu_read(struct nas_display_device *dev,uint64_t offset,
                         uint8_t *dst,uint64_t bytes) {
    memcpy(dst,(uint8_t *)dev->backend+offset,(size_t)bytes);
    return bytes;
}
static uint64_t gpu_write(struct nas_display_device *dev,uint64_t offset,
                          const uint8_t *src,uint64_t bytes) {
    memcpy((uint8_t *)dev->backend+offset,src,(size_t)bytes);
    return bytes;
}
static int gpu_present(struct nas_display_device *dev) {
    assert(dev->backend==gpu_pixels);
    ++presentations;
    return 0;
}
static const struct nas_display_ops gpu_ops={
    .read_at=gpu_read,.write_at=gpu_write,.present=gpu_present
};
static struct nas_display_device gpu={
    .name="virtio-gpu-2d",.backend=gpu_pixels,
    .mode={.width=4,.height=4,.pitch=16,.bpp=32,
           .red_size=8,.red_shift=16,.green_size=8,.green_shift=8,
           .blue_size=8,.blue_shift=0},
    .ops=&gpu_ops
};
static struct nas_fb_boot_info boot(void) {
    return (struct nas_fb_boot_info){
        .address=boot_pixels,.width=4,.height=4,.pitch=16,.bpp=32,
        .memory_model=1,.red_size=8,.red_shift=16,
        .green_size=8,.green_shift=8,.blue_size=8,.blue_shift=0
    };
}
static void check_live_node(bool expected) {
    struct nas_chrdev_info info;
    const struct nas_chrdev_provider *provider;
    struct nas_chrdev_node *node;
    int rc=nas_chrdev_describe("/dev/fb0",&info,&provider,&node);
    if(!expected) {assert(rc==-ENOENT);return;}
    assert(rc==0 && info.rdev==(29U<<8) && node && !provider);
    assert(nas_chrdev_node_pin(node)==0);
    nas_chrdev_node_put(node);
}
static void test_handoff(void) {
    assert(nas_display_present()==-ENODEV);
    /* The endpoint is registered BEFORE either firmware or GPU exists. */
    assert(nas_fbdev_chrdev_init()==0);
    check_live_node(false);
    struct nas_fb_boot_info fb=boot();
    assert(nas_fbdev_boot_init(&fb));
    check_live_node(true);
    assert(nas_display_present()==0); /* Optional bootfb callback. */
    assert(!presentations);

    /* One legitimate replacement in bootstrap only. */
    assert(nas_display_boot_takeover(&gpu));
    assert(nas_display_primary()==&gpu);
    assert(nas_display_length()==sizeof(gpu_pixels));
    struct nas_fb_fix_screeninfo fix;
    assert(nas_fbdev_fix_screeninfo(&fix));
    assert(!strncmp(fix.id,"virtio-gpu-2d",sizeof(fix.id)));
    assert(fix.smem_len==64 && fix.line_length==16);
    check_live_node(true);
    /* FileOps may chunk logical writes into 1 KiB. Presentation must NOT
     * happen for each backend write callback; the scheduler batches it. */
    uint8_t src[]={1,2,3,4},dst[sizeof(src)]={0};
    for(unsigned i=0;i<sizeof(src);i++)
        assert(nas_fbdev_write_at(i,&src[i],1)==1);
    assert(memcmp(gpu_pixels,src,sizeof(src))==0);
    assert(memcmp(boot_pixels,src,sizeof(src))!=0);
    assert(!presentations);
    assert(nas_display_present()==0 && presentations==1);
    assert(nas_fbdev_read_at(0,dst,sizeof(dst))==sizeof(dst));
    assert(memcmp(dst,src,sizeof(src))==0);
    assert(!nas_display_boot_takeover(&gpu));
    assert(!nas_display_register_primary(&gpu));
    assert(!nas_fbdev_boot_init(&fb));

    nas_display_lock_primary();
    assert(!nas_display_boot_takeover(&gpu));
    assert(!nas_display_register_primary(&gpu));
    assert(!nas_fbdev_boot_init(NULL));
    assert(nas_display_primary()==&gpu);
    check_live_node(true);
}
static void test_locked(void) {
    assert(nas_fbdev_chrdev_init()==0);
    check_live_node(false);
    struct nas_fb_boot_info fb=boot();
    assert(nas_fbdev_boot_init(&fb));
    nas_display_lock_primary();
    /* Even firmware retry cannot remove a scanout after userspace starts. */
    assert(!nas_display_boot_takeover(&gpu));
    assert(!nas_fbdev_boot_init(NULL));
    assert(nas_display_primary()!=&gpu);
    check_live_node(true);
}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    if(!strcmp(argv[1],"handoff"))test_handoff();
    else if(!strcmp(argv[1],"locked"))test_locked();
    else return 2;
    puts("[M177 DISPLAY HANDOFF PASS]");
    return 0;
}
