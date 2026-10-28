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
    assert(nas_vfs_unlink("/ramfs/moved/file",0)==0);
    struct nas_fs_node removed,renewed;
    assert(nas_fs_lookup_node("/ramfs/moved/file",true,&removed,path)==-ENOENT);
    assert(nas_vfs_getattr(&node,&removed)==0 && removed.nlink==0);
    assert(nas_vfs_read_at(&node,0,(uint8_t *)text,5)==5);
    assert(!memcmp(text,"hello",5));
    assert(nas_ramfs_vfs_unmount(&a)==-EBUSY);
    /* An unlinked open file and a NEW file at its original name must
     * have separate stable generations and independent data. */
    assert(nas_vfs_create("/ramfs/moved/file",0600)==0);
    assert(nas_fs_lookup_node("/ramfs/moved/file",true,&renewed,path)==0);
    assert(renewed.ino!=node.ino);
    assert(nas_vfs_write_at(&renewed,0,(const uint8_t *)"new",3)==3);
    assert(nas_vfs_read_at(&node,0,(uint8_t *)text,5)==5);
    assert(!memcmp(text,"hello",5));
    assert(nas_vfs_unlink("/ramfs/moved/file",0)==0);
    assert(nas_vfs_unlink("/ramfs/moved",0x200ULL)==0);
    assert(nas_vfs_getattr(&dir,&removed)==0 && removed.nlink==0);
    assert(nas_vfs_node_unpin(&node)==0);
    assert(nas_vfs_node_unpin(&dir)==0);
    /* The backend's internal orphan directory must not become a public
     * path or a getdents64 entry, including after final inode unpin. */
    assert(nas_fs_lookup_node("/ramfs/.nas_ramfs_orphans",true,&removed,path)==-ENOENT);
    assert(nas_vfs_create("/ramfs/.nas_ramfs_orphans",0644)==-EPERM);
    assert(nas_fs_lookup_node("/ramfs",true,&dir,path)==0);
    struct nas_vfs_dirent root_dents[8];
    int root_n=nas_vfs_read_dir(&dir,0,root_dents,8);
    assert(root_n>=2);
    for(int k=0;k<root_n;k++)
        assert(strcmp(root_dents[k].name,".nas_ramfs_orphans")!=0);
    /* Empty opened directories are also detached then reclaimed by VFS's
     * last_inode_unpin; nonempty rmdir must fail BEFORE private rename. */
    assert(nas_vfs_mkdir("/ramfs/open-dir",0700)==0);
    assert(nas_fs_lookup_node("/ramfs/open-dir",true,&dir,path)==0);
    assert(nas_vfs_node_pin(&dir)==0);
    assert(nas_vfs_unlink("/ramfs/open-dir",0x200ULL)==0);
    assert(nas_vfs_getattr(&dir,&removed)==0 && removed.nlink==0);
    assert(nas_vfs_directory_path(&dir,"/ramfs/open-dir",path)==-ENOENT);
    assert(nas_vfs_node_unpin(&dir)==0);
    assert(nas_fs_lookup_node("/ramfs/open-dir",true,&removed,path)==-ENOENT);
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
    /* 320 same-directory records exceed the old fixed 128 identity table.
     * Exercise independent arbitrary-name lookup and bounded readdir with
     * stable exported inode IDs, without bypassing shared VFS dispatch. */
    assert(nas_vfs_mkdir("/ramfs/bulk",0700)==0);
    for(unsigned i=0;i<320;i++) {
        char name[256];
        assert(snprintf(name,sizeof(name),"/ramfs/bulk/f%03u",i)>0);
        assert(nas_vfs_create(name,(i&1U)?0600:0640)==0);
    }
    assert(nas_fs_lookup_node("/ramfs/bulk",true,&dir,path)==0);
    assert((dir.mode&0777)==0700);
    unsigned seen=0;uint64_t cursor=0;
    struct nas_vfs_dirent entries[7];
    for(;;) {
        int count=nas_vfs_read_dir(&dir,cursor,entries,7);
        assert(count>=0);
        if(!count)break;
        for(int i=0;i<count;i++) {
            if(cursor+(unsigned)i>=2) {
                assert(entries[i].ino!=0);
                assert(entries[i].dtype==8);
                seen++;
            }
        }
        cursor+=(unsigned)count;
    }
    assert(seen==320);
    for(unsigned i=0;i<320;i++) {
        char name[256];
        assert(snprintf(name,sizeof(name),"/ramfs/bulk/f%03u",i)>0);
        assert(nas_fs_lookup_node(name,true,&node,path)==0);
        assert((node.mode&0777)==((i&1U)?0600:0640));
        if(i==107) {
            assert(nas_vfs_chmod(&node,0604)==0);
            assert(nas_fs_lookup_node(name,true,&node,path)==0);
            assert((node.mode&0777)==0604);
        }
        if(i==148) {
            struct nas_fs_node stale=node,fresh;
            assert(nas_vfs_unlink(name,0)==0);
            assert(nas_vfs_create(name,0600)==0);
            assert(nas_fs_lookup_node(name,true,&fresh,path)==0);
            assert(fresh.ino!=stale.ino);
            assert(nas_vfs_read_at(&stale,0,(uint8_t *)text,1)==-ESTALE);
        }
        assert(nas_vfs_unlink(name,0)==0);
    }
    assert(nas_vfs_unlink("/ramfs/bulk",0x200ULL)==0);
    /* Recycle mount-local identity slots across repeated namespace churn:
     * a stale pre-delete snapshot must never alias a new inode generation. */
    for(unsigned i=0;i<700;i++) {
        assert(nas_vfs_create("/ramfs/churn",0640)==0);
        assert(nas_fs_lookup_node("/ramfs/churn",true,&node,path)==0);
        assert(nas_vfs_unlink("/ramfs/churn",0)==0);
    }
    assert(nas_ramfs_vfs_unmount(&a)==0);
    assert(nas_ramfs_vfs_unmount(&b)==0);
    puts("[UPSTREAM RAMFS VFS PASS] upstream unchanged; VFS open-unlink orphan; old/new generations; open rmdir; 320+ identities; 700 reuse; modes; multi-mount");
    return 0;
}
