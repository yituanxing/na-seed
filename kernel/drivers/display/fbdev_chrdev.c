#include "fbdev.h"
#include "../core/chrdev.h"
#include "../../fd.h"

/* Publish a STABLE devfs node even when firmware has no boot framebuffer.
 * The presence callback changes dynamically when the first GPU binds (or
 * takes over bootfb); no device names are fabricated in syscall dispatch. */
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
    return nas_chrdev_node_register(&framebuffer_node);
}
