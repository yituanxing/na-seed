#ifndef NASEED_BOOT_IMAGE_H
#define NASEED_BOOT_IMAGE_H

#include <stdbool.h>
#include <stdint.h>
#include "fs/lookup.h"
#include "task/elf.h"

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
    /* When an explicit block-backed root is selected, both executable
     * sources are pinned VFS inodes instead of Limine/CPIO byte slices. */
    struct nas_fs_node source, loader;
    bool source_pinned, loader_pinned;
};

struct limine_file;
/* Legacy rootless direct-module bootstrap endpoints. The module must
 * actually be an ELF supplied by Limine, and its name must match the
 * requested synthetic /bin entry or the architecture's musl interpreter.
 * This is a boot-source property, never a per-task executable-name policy. */
const struct limine_file *nas_boot_limine_module_path(const char *path);

/* Select and validate the Limine modules / initramfs source. selected is
 * NULL without an explicit naseed.argv, preserving the legacy boot choice.
 * Return NULL on success; otherwise a static fatal error for caller.
 */
const char *nas_boot_prepare(const char *selected,
                             struct nas_boot_image *out);
const char *nas_boot_prepare_vfs_root(const char *selected,
                                      struct nas_boot_image *out);
void nas_boot_image_elf(const struct nas_boot_image *source,
                        struct elf_image *main_image,
                        struct elf_image *interpreter_image);
void nas_boot_image_release(struct nas_boot_image *source);
#endif
