#ifndef NASEED_BOOT_IMAGE_H
#define NASEED_BOOT_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

/* One selected *real* ELF and optional interpreter, as backed by Limine
 * modules or by indexed, immutable CPIO bytes. This is a boot handoff
 * descriptor, not a filesystem, task, ELF loader, or userspace ABI.
 */
struct nas_boot_image {
    const uint8_t *image;
    uint64_t size;
    const uint8_t *interpreter;
    uint64_t interpreter_size;
    bool has_interpreter;
    bool bash, busybox, alpine;
};

/* Select and validate the Limine modules / initramfs source. selected is
 * NULL without an explicit naseed.argv, preserving the legacy boot choice.
 * Return NULL on success; otherwise a static fatal error for caller.
 */
const char *nas_boot_prepare(const char *selected,
                             struct nas_boot_image *out);
#endif
