#include "boot_image.h"
#include "bootfs.h"
#include "lib/string.h"
#include "arch/abi.h"
#include <limine.h>

extern const struct limine_file *nas_user_module(void);
extern const struct limine_file *nas_module(uint64_t index);
extern void nas_print(const char *);

static const char *basename(const char *path) {
    const char *last = strrchr(path, '/');
    return last ? last + 1 : path;
}
static bool busybox_module(const struct limine_file *file) {
    return file && file->path && strcmp(basename(file->path), "busybox") == 0;
}
static bool bash_module(const struct limine_file *file) {
    if (!file || !file->path) return false;
    /* Match the original substring policy, without looking past the NUL. */
    for (const char *p = file->path; *p; p++)
        if (strncmp(p, "bash", 4) == 0) return true;
    return false;
}
static bool newc_archive(const struct limine_file *file) {
    if (!file || !file->address || file->size < 6) return false;
    /* Header is binary data: length checked above, do not assume NUL. */
    return memcmp(file->address, "070701", 6) == 0;
}
const char *nas_boot_prepare(const char *selected,
                             struct nas_boot_image *out) {
    if (!out) return "missing boot image descriptor";
    *out = (struct nas_boot_image){0};
    const struct limine_file *module = nas_user_module();
    if (!module) return "no Limine userspace module";
    bool archive_only = newc_archive(module);
    const struct limine_file *archive =
        archive_only ? module : nas_module(2);
    if (archive) {
        if (!bootfs_mount(archive->address, archive->size))
            return "invalid CPIO initramfs module";
        nas_print("[FS] CPIO newc read-only archive mounted\n");
    }
    if (archive_only) {
        /* This is *selection* from the actual archive index, not an
         * alternate hardcoded executable or a writable initramfs.
         */
        const char *path = selected ? selected : "/bin/busybox";
        if (path[0] != '/')
            return "CPIO executable must be an absolute path";
        const struct bootfs_entry *app = bootfs_find(path);
        if (!app) return "CPIO executable path not found";
        if (bootfs_is_dir(app) || !(app->mode & 0111U))
            return "CPIO selected path is not an executable regular file";
        out->image = app->data;
        out->size = app->size;
        const struct bootfs_entry *ldso =
            bootfs_find(NAS_ELF_INTERP);
        if (ldso && !bootfs_is_dir(ldso)) {
            out->interpreter = ldso->data;
            out->interpreter_size = ldso->size;
            out->has_interpreter = true;
        }
        out->busybox = strcmp(basename(path), "busybox") == 0;
        /* ELF64 little-endian e_type is at offset 16; an unmodified
         * Alpine ET_DYN is distinguished from a static Debian ET_EXEC.
         * Do not dereference an unvalidated/undersized ELF header.
         */
        out->alpine = out->busybox && app->size >= 64 &&
            app->data[16] == 3 && app->data[17] == 0;
        nas_print("[M5] CPIO executable selected: ");
        nas_print(path);
        nas_print("\n");
        nas_print("[M5] actual ELF image sourced from CPIO file entry\n");
        return 0;
    }
    /* Legacy M1-M4: direct real ELF module; optional actual musl module. */
    out->image = (const uint8_t *)module->address;
    out->size = module->size;
    out->bash = bash_module(module);
    out->busybox = busybox_module(module);
    out->alpine = out->busybox && nas_module(1) != 0;
    if (archive && !out->alpine)
        return "archive file requires real Alpine BusyBox runtime";
    if (out->alpine) {
        const struct limine_file *ldso = nas_module(1);
        out->interpreter = (const uint8_t *)ldso->address;
        out->interpreter_size = ldso->size;
        out->has_interpreter = true;
    }
    if (out->bash) nas_print("[M2] real GNU Bash module discovered\n");
    if (out->busybox) nas_print("[M3] real BusyBox module discovered\n");
    return 0;
}
