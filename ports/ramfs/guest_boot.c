/* Explicit opt-in original embedded RamFS guest, independent of tmpfs.
 * Exercise real VFS inode/namespace/mount operations, not upstream bare API
 * stubs. The same object links on all three native NaSeed architectures. */
#include "vfs_port.h"
#include "../../kernel/fs/lookup.h"
#include "../../kernel/fs/vfs.h"
#include "../../kernel/lib/string.h"
#include "../../kernel/errno.h"
extern void nas_print(const char *);
static struct nas_ramfs_vfs guest;
static bool active;
bool nas_ramfs_guest_boot_mount(void) {
    if(nas_ramfs_vfs_mount(&guest,"/ramfs"))return false;
    active=true;
    /* Directory mutation and file I/O must work through the common VFS. */
    if(nas_vfs_mkdir("/ramfs/old",0755))return false;
    if(nas_vfs_create("/ramfs/old/hello",0644))return false;
    struct nas_fs_node f,d;
    char canonical[256],readback[8]={0};
    if(nas_fs_lookup_node("/ramfs/old/hello",true,&f,canonical))return false;
    if(nas_fs_lookup_node("/ramfs/old",true,&d,canonical))return false;
    if(nas_vfs_node_pin(&f) || nas_vfs_node_pin(&d))return false;
    /* The real original RamFS guest must exercise the SAME owned Path
     * as the shared FS cwd/openat layer, not a provider's private reverse
     * inode->path lookup. Keep it alive across the directory rename. */
    struct nas_vfs_path opened_dir={0};
    if(nas_vfs_path_bind(&d,"/ramfs/old",&opened_dir))return false;
    if(nas_vfs_write_at(&f,0,(const uint8_t *)"memory",6)!=6)return false;
    if(nas_vfs_rename("/ramfs/old","/ramfs/moved",0))return false;
    if(nas_vfs_read_at(&f,0,(uint8_t *)readback,6)!=6 ||
       memcmp(readback,"memory",6))return false;
    if(nas_vfs_path_string(&opened_dir,canonical) ||
       strcmp(canonical,"/ramfs/moved"))return false;
    /* Original vendor frees on unlink: the separate port now keeps a
     * hidden orphan until the LAST VFS File lease goes away. Inode
     * identity and data must survive removal of its public dentry. */
    if(nas_vfs_unlink("/ramfs/moved/hello",0))return false;
    if(nas_fs_lookup_node("/ramfs/moved/hello",true,&(struct nas_fs_node){0},canonical)!=-ENOENT)
        return false;
    if(nas_vfs_read_at(&f,0,(uint8_t *)readback,6)!=6 ||
       memcmp(readback,"memory",6))return false;
    struct nas_fs_node orphan_meta;
    if(nas_vfs_getattr(&f,&orphan_meta) || orphan_meta.nlink)return false;
    if(nas_vfs_create("/ramfs/moved/hello",0600))return false;
    if(nas_vfs_unlink("/ramfs/moved/hello",0))return false;
    if(nas_vfs_unlink("/ramfs/moved",0x200ULL))return false;
    /* Unlinked but still open native directory remains an inode, but its
     * Dentry must no longer publish a reachable namespace pathname. */
    if(nas_vfs_path_string(&opened_dir,canonical)!=-ENOENT)return false;
    if(nas_vfs_path_put(&opened_dir))return false;
    if(nas_vfs_node_unpin(&f) || nas_vfs_node_unpin(&d))return false;
    nas_print("[PASS] real upstream RamFS: VFS create/IO/rename/open-pin/unlink/rmdir\n");
    return true;
}
bool nas_ramfs_guest_boot_shutdown(void) {
    if(!active)return true;
    if(nas_ramfs_vfs_unmount(&guest))return false;
    active=false;
    nas_print("[RAMFS GUEST] original standalone RamFS clean VFS unmount\n");
    return true;
}
