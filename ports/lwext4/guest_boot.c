/* Real Limine boot module -> kernel VFS -> lwext4 -> RamDisk path.
 * This is an opt-in source-level capability, never a replacement for the
 * common VFS or userspace syscall route. Build for all supported ISAs. */
#include "guest_vfs.h"
#include "../../kernel/fs/lookup.h"
#include "../../kernel/fs/vfs.h"
#include "../../kernel/lib/string.h"
#include <limine.h>
#include <stdint.h>
#include <stdbool.h>

extern const struct limine_file *nas_module(uint64_t index);
extern void nas_print(const char *);
static struct nas_lwext4_mount boot_mount;

static bool ext4_module(const struct limine_file *file) {
    if(!file || !file->address || file->size < 1082 ||
       file->size > 128U*1024U*1024U || (file->size & 511U))return false;
    const uint8_t *data=(const uint8_t *)file->address;
    /* On-disk ext superblock magic at 1024+0x38, not a module filename. */
    return data[1080]==0x53 && data[1081]==0xef;
}
bool nas_lwext4_guest_boot_mount(void) {
    const struct limine_file *file=NULL;
    for(uint64_t i=1;i<64;i++) {
        const struct limine_file *next=nas_module(i);
        if(!next)break;
        if(ext4_module(next)) {
            if(file)return false; /* More than one boot disk needs config. */
            file=next;
        }
    }
    if(!file)return true; /* Opt-in kernel still boots without a disk. */
    int rc=nas_lwext4_ramdisk_mount(&boot_mount,"limine-ext4",
                                    "limine-ext4-dev","/mnt",
                                    file->address,file->size);
    if(rc)return false;
    char path[256];
    struct nas_fs_node node;
    /* This is the actual kernel mount table, pathwalk and upstream lookup. */
    if(nas_fs_lookup_node("/mnt",true,&node,path) || !node.is_dir ||
       !node.filesystem || strcmp(node.filesystem->name,"lwext4"))
        return false;
    nas_print("[LWEXT4 GUEST] actual Limine ext4 image mounted at /mnt\n");

    /* The acceptance fixture is optional for arbitrary user-supplied disks.
     * A separate real-QEMU gate requires this marker from the known image. */
    static const char expected[]="NaSeed VFS and BlockDevice real lwext4 data\n";
    if(nas_fs_lookup_node("/mnt/sandbox/hardlink",true,&node,path)==0) {
        uint8_t contents[sizeof(expected)];
        if(node.size!=sizeof(expected)-1 ||
           nas_vfs_read_at(&node,0,contents,sizeof(contents))!=(int64_t)node.size ||
           memcmp(contents,expected,sizeof(expected)-1))
            return false;
        nas_print("[PASS] LWEXT4 guest VFS real ext4 inode read\n");
    }
    return true;
}
