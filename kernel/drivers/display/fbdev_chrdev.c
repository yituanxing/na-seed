#include "fbdev.h"
#include "../core/chrdev.h"
#include "../../fd.h"

/* The display class publishes the fbdev endpoint, not the syscall decoder.
 * The underlying scanout may be the boot framebuffer or any future driver
 * registered with DisplayDevice. A missing display publishes no /dev/fb0. */
static bool display_present(unsigned unit) {
    return unit==0U && nas_fbdev_available();
}

static struct nas_chrdev_node framebuffer_node={
    .path="/dev/fb0",
    .info={
        .fd_kind=FD_FRAMEBUFFER,
        .unit=0,
        .dev=2,
        .ino=2900,
        .rdev=(29U<<8),
        .mode=0020666U
    },
    .present=display_present
};

int nas_fbdev_chrdev_init(void) {
    if(!nas_fbdev_available())return 0;
    return nas_chrdev_node_register(&framebuffer_node);
}
