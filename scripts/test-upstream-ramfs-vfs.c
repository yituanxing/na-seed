/* Compile the existing real VFS host fixture once, then mount two NEW,
 * independent upstream RamFS instances via the same provider registry. */
#define main nas_original_vfs_fixture_main
#include "test-vfs-provider.c"
#undef main
#include "../ports/ramfs/vfs_port.h"
int main(void) {
    assert(nas_original_vfs_fixture_main()==0);
    static struct nas_ramfs_vfs a,b;
    struct nas_fs_node node,dir,other;
    char path[256];
    char text[16]={0};
    assert(nas_ramfs_vfs_mount(&a,"/ramfs")==0);
    assert(nas_ramfs_vfs_mount(&b,"/ramfs-two")==0);
    assert(nas_fs_lookup_node("/ramfs",true,&dir,path)==0);
    assert(dir.is_dir && !strcmp(dir.filesystem->name,"upstream-ramfs"));
    assert(nas_vfs_mkdir("/ramfs/parent",0755)==0);
    assert(nas_vfs_create("/ramfs/parent/file",0644)==0);
    assert(nas_vfs_create("/ramfs-two/file",0644)==0);
    assert(nas_fs_lookup_node("/ramfs/parent/file",true,&node,path)==0);
    assert(nas_fs_lookup_node("/ramfs-two/file",true,&other,path)==0);
    assert(node.mount!=other.mount && node.inode!=other.inode);
    assert(nas_vfs_node_pin(&node)==0);
    assert(nas_vfs_write_at(&node,0,(const uint8_t *)"hello",5)==5);
    assert(nas_vfs_node_size(&node)==5);
    assert(nas_fs_lookup_node("/ramfs/parent",true,&dir,path)==0);
    assert(nas_vfs_node_pin(&dir)==0);
    assert(nas_vfs_rename("/ramfs/parent","/ramfs/moved",0)==0);
    assert(nas_vfs_directory_path(&dir,"/ramfs/parent",path)==0);
    assert(!strcmp(path,"/ramfs/moved"));
    assert(nas_vfs_read_at(&node,0,(uint8_t *)text,5)==5);
    assert(!memcmp(text,"hello",5));
    assert(nas_vfs_unlink("/ramfs/moved/file",0)==-EBUSY);
    assert(nas_ramfs_vfs_unmount(&a)==-EBUSY);
    assert(nas_vfs_node_unpin(&node)==0);
    assert(nas_vfs_node_unpin(&dir)==0);
    assert(nas_vfs_unlink("/ramfs/moved/file",0)==0);
    assert(nas_fs_lookup_node("/ramfs/moved/file",true,&node,path)==-ENOENT);
    assert(nas_vfs_unlink("/ramfs/moved",0x200ULL)==0);
    assert(nas_vfs_read_at(&other,0,(uint8_t *)text,5)==0);
    assert(nas_vfs_write_at(&other,0,(const uint8_t *)"other",5)==5);
    assert(nas_vfs_read_at(&other,0,(uint8_t *)text,5)==5);
    assert(!memcmp(text,"other",5));
    /* POSIX rename-overwrite is not implemented by this upstream library:
     * reject without unlinking victim. Document an actual VFS capability
     * boundary rather than silently declaring two backends equivalent. */
    assert(nas_vfs_create("/ramfs-two/victim",0644)==0);
    assert(nas_vfs_rename("/ramfs-two/file","/ramfs-two/victim",0)==-EOPNOTSUPP);
    assert(nas_fs_lookup_node("/ramfs-two/victim",true,&node,path)==0);
    assert(nas_vfs_unlink("/ramfs-two/victim",0)==0);
    assert(nas_vfs_unlink("/ramfs-two/file",0)==0);
    assert(nas_ramfs_vfs_unmount(&a)==0);
    assert(nas_ramfs_vfs_unmount(&b)==0);
    puts("[UPSTREAM RAMFS VFS PASS] original vendor; two mounts; inode identity; rename; I/O; busy unlink/umount");
    return 0;
}
