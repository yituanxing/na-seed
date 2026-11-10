#include "fbdev.h"
#include "display.h"
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

/* Every successful open of /dev/fb0 pins this node exactly once for its
 * SHARED OpenFile description, including aliases inherited through dup,
 * fork or SCM_RIGHTS. The common final-close path drops that same pin.
 * DisplayDevice teardown consults this existing class ownership to avoid
 * releasing a GPU while any userspace File can still reach its pixels.
 * O_PATH pins are conservatively counted too: lifetime must never depend
 * on a guessed access-mode subset of the shared fd namespace. */
static unsigned fbdev_open_pins(void) {
    return framebuffer_node.pins;
}

int nas_fbdev_chrdev_init(void) {
    int rc=nas_chrdev_node_register(&framebuffer_node);
    if(rc)return rc;
    /* The display class remains independent of chrdev linkage, while the
     * production fbdev personality supplies its one true consumer count.
     * No duplicate File/FD pin registry is introduced here. */
    rc=nas_display_set_open_pins_callback(fbdev_open_pins);
    if(rc)(void)nas_chrdev_node_unregister(&framebuffer_node);
    return rc;
}
