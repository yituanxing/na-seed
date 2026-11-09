#include "boot_image.h"
#include "bootfs.h"
#include "lib/string.h"
#include "arch/abi.h"
#include "fs/lookup.h"
#include "task/elf_segments.h"
#include <limine.h>

extern const struct limine_file *nas_user_module(void);
extern const struct limine_file *nas_module(uint64_t index);
extern void nas_print(const char *);

static const char *basename(const char *path) {
    const char *last = strrchr(path, '/');
    return last ? last + 1 : path;
}
static bool real_elf_module(const struct limine_file *file) {
    static const uint8_t magic[4] = {0x7f, 'E', 'L', 'F'};
    return file && file->address && file->path && file->size >= 4 &&
        memcmp(file->address, magic, sizeof(magic)) == 0;
}

const struct limine_file *nas_boot_limine_module_path(const char *path) {
    if (!path || path[0] != '/') return 0;
    const struct limine_file *program = nas_module(0);
    /* Direct ELF modules have no mounted inode in the original M1-M4
     * rootless boot. Derive their visibility from the actual module,
     * never from the name of the currently executing task. A CPIO module
     * must not masquerade as an ELF file through this compatibility path. */
    if (real_elf_module(program) && strncmp(path, "/bin/", 5) == 0 &&
        strcmp(path + 5, basename(program->path)) == 0)
        return program;
    const struct limine_file *interpreter = nas_module(1);
    if (real_elf_module(interpreter) &&
        strcmp(basename(interpreter->path), basename(NAS_ELF_INTERP)) == 0 &&
        (strcmp(path, NAS_ELF_INTERP) == 0 ||
         strcmp(path, NAS_ELF_LIBC_ALIAS) == 0))
        return interpreter;
    return 0;
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
        char app_path[256];
        if (bootfs_resolve(path,true,app_path))
            return "CPIO executable path not found";
        const struct bootfs_entry *app = bootfs_find(app_path);
        if (!app) return "CPIO executable path not found";
        if ((app->mode&0170000U)!=0100000U || !(app->mode & 0111U))
            return "CPIO selected path is not an executable regular file";
        out->image = app->data;
        out->size = app->size;
        bool wants_loader=false;
        const struct elf_image main={.data=out->image,.size=out->size};
        if (!nas_elf_interpreter_path(&main,NAS_ELF_MACHINE,
                out->interpreter_path,sizeof(out->interpreter_path),
                &wants_loader))
            return "CPIO selected executable has invalid ELF interpreter";
        char resolved_loader[256];
        const struct bootfs_entry *ldso = NULL;
        if (wants_loader &&
            !bootfs_resolve(out->interpreter_path,true,resolved_loader))
            ldso=bootfs_find(resolved_loader);
        if (ldso && (ldso->mode&0170000U)==0100000U &&
            (ldso->mode&0111U)) {
            out->interpreter = ldso->data;
            out->interpreter_size = ldso->size;
            out->has_interpreter = true;
        } else if (wants_loader) return "CPIO ELF interpreter not executable";
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
    const struct elf_image main={.data=out->image,.size=out->size};
    bool wants_loader=false;
    if (!nas_elf_interpreter_path(&main,NAS_ELF_MACHINE,
            out->interpreter_path,sizeof(out->interpreter_path),
            &wants_loader))
        return "invalid direct ELF interpreter";
    out->bash = bash_module(module);
    out->busybox = busybox_module(module);
    out->alpine = out->busybox && nas_module(1) != 0;
    if (archive && !out->alpine)
        return "archive file requires real Alpine BusyBox runtime";
    if (out->alpine && wants_loader &&
        strcmp(out->interpreter_path,NAS_ELF_INTERP)==0) {
        const struct limine_file *ldso = nas_module(1);
        out->interpreter = (const uint8_t *)ldso->address;
        out->interpreter_size = ldso->size;
        out->has_interpreter = true;
    }
    if (out->bash) nas_print("[M2] real GNU Bash module discovered\n");
    if (out->busybox) nas_print("[M3] real BusyBox module discovered\n");
    return 0;
}

/* M191 uses the SAME mounted VFS and ELF reader as later execve; the boot
 * policy merely selects its original PID1 image before entering ring 3.
 * No userspace address is read on the bootstrap kernel stack. */
static bool root_image_read(const void *context,uint64_t off,
                            uint8_t *destination,uint64_t length) {
    const struct nas_fs_node *node=context;
    return nas_vfs_read_at(node,off,destination,length)==(int64_t)length;
}
void nas_boot_image_release(struct nas_boot_image *source) {
    if(!source)return;
    if(source->loader_pinned && nas_vfs_node_unpin(&source->loader))
        nas_print("[M191] interpreter inode lease release failed\n");
    if(source->source_pinned && nas_vfs_node_unpin(&source->source))
        nas_print("[M191] boot inode lease release failed\n");
    source->source_pinned=source->loader_pinned=false;
}
const char *nas_boot_prepare_vfs_root(const char *selected,
                                      struct nas_boot_image *out) {
    if(!out || !selected || selected[0]!='/')
        return "M191 requires an absolute original Alpine PID1 path";
    *out=(struct nas_boot_image){0};
    char canonical[256];
    if(nas_fs_lookup_node(selected,true,&out->source,canonical) ||
       (out->source.mode&0170000U)!=0100000U ||
       !(out->source.mode&0111U))
        return "M191 root PID1 executable not found or not executable";
    if(nas_vfs_node_pin(&out->source))
        return "M191 root PID1 inode lease failed";
    out->source_pinned=true;
    out->image=out->source.data;
    out->size=out->source.size;
    struct elf_image main={.data=out->image,.size=out->size};
    if (!main.data) {
        main.read_at=root_image_read;
        main.context=&out->source;
    }
    bool wants_loader=false;
    if (!nas_elf_interpreter_path(&main,NAS_ELF_MACHINE,
            out->interpreter_path,sizeof(out->interpreter_path),
            &wants_loader)) {
        nas_boot_image_release(out);
        return "M191 root PID1 has invalid ELF interpreter";
    }
    if (wants_loader) {
        if (nas_fs_lookup_node(out->interpreter_path,true,
                               &out->loader,canonical) ||
            (out->loader.mode&0170000U)!=0100000U ||
            !(out->loader.mode&0111U) ||
            nas_vfs_node_pin(&out->loader)) {
            nas_boot_image_release(out);
            return "M191 original PID1 ELF interpreter not executable";
        }
        out->loader_pinned=out->has_interpreter=true;
        out->interpreter=out->loader.data;
        out->interpreter_size=out->loader.size;
    }
    nas_print("[M191 ROOT] original Alpine PID1 from mounted ext4: ");
    nas_print(selected);nas_print("\n");
    return NULL;
}
void nas_boot_image_elf(const struct nas_boot_image *source,
                        struct elf_image *main_image,
                        struct elf_image *interpreter_image) {
    *main_image=(struct elf_image){.data=source->image,.size=source->size};
    *interpreter_image=(struct elf_image){.data=source->interpreter,
                                        .size=source->interpreter_size};
    if(source->source_pinned && !source->image) {
        main_image->read_at=root_image_read;
        main_image->context=&source->source;
    }
    if(source->loader_pinned && !source->interpreter) {
        interpreter_image->read_at=root_image_read;
        interpreter_image->context=&source->loader;
    }
}
