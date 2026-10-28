/* Independent original littlefs VFS mounted by an explicit, opt-in
 * Limine RAM image. No physical persistent writes are claimed. */
#include "vfs_port.h"
#include "../../kernel/block/ramdisk.h"
#include "../../kernel/fs/lookup.h"
#include "../../kernel/lib/string.h"
#include <limine.h>
#include <stdint.h>
#include <stdbool.h>
extern const struct limine_file *nas_module(uint64_t);
extern void nas_print(const char *);
static struct nas_ramdisk ram;
static struct nas_littlefs_vfs guest;
static bool active;
static bool named_littlefs(const struct limine_file *file) {
    if(!file || !file->path || !file->address || !file->size ||
       (file->size%512U) || file->size>32U*1024U*1024U)return false;
    const char *last=strrchr(file->path,'/');
    return !strcmp(last?last+1:file->path,"naseed-littlefs.img");
}
bool nas_littlefs_guest_boot_mount(void) {
    const struct limine_file *image=NULL;
    for(uint64_t i=1;i<64;i++) {
        const struct limine_file *file=nas_module(i);
        if(!file)break;
        if(!named_littlefs(file))continue;
        if(image)return false; /* ambiguous device selection */
        image=file;
    }
    if(!image)return true; /* opt-in build can still boot normally */
    int rc=nas_ramdisk_attach(&ram,"limine-littlefs",
                             image->address,image->size,512,true);
    if(rc)return false;
    rc=nas_littlefs_vfs_mount(&guest,"limine-littlefs","/flash");
    if(rc) {
        (void)nas_ramdisk_detach(&ram);
        return false;
    }
    active=true;
    nas_print("[LITTLEFS GUEST] real upstream littlefs mounted through NaSeed VFS at /flash (RAM only, RO)\n");
    struct nas_fs_node root,node;
    char resolved[256];
    if(nas_fs_lookup_node("/flash",true,&root,resolved) ||
       !root.is_dir || !root.filesystem ||
       strcmp(root.filesystem->name,"littlefs"))
        return false;
    if(nas_fs_lookup_node("/flash/source/program",true,&node,resolved))
        return false;
    static const char expected[]="Second independent real filesystem through NaSeed VFS";
    uint8_t buffer[sizeof(expected)]={0};
    if(node.size!=sizeof(expected)-1 ||
       nas_vfs_read_at(&node,0,buffer,sizeof(buffer))!=(int64_t)node.size ||
       memcmp(buffer,expected,sizeof(expected)-1))
       return false;
    nas_print("[PASS] LITTLEFS native kernel VFS inode read from original filesystem\n");
    return true;
}
bool nas_littlefs_guest_boot_shutdown(void) {
    if(!active)return true;
    if(nas_littlefs_vfs_unmount(&guest))return false;
    if(nas_ramdisk_detach(&ram))return false;
    active=false;
    nas_print("[LITTLEFS GUEST] clean unmount and BlockDevice release\n");
    return true;
}
