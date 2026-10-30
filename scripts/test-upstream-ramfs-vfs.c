/* Compile the existing real VFS host fixture once, then mount two NEW,
 * independent upstream RamFS instances via the same provider registry. */
#define main nas_original_vfs_fixture_main
#include "test-vfs-provider.c"
#undef main
#include "../ports/ramfs/vfs_port.h"
/* The production provider reads the common RTC-anchored kernel clock.
 * This host-only fixture supplies a deterministic timestamp oracle. */
static bool fake_realtime_available=true;
bool nas_time_realtime_ns(uint64_t *out) {
    if(!out || !fake_realtime_available)return false;
    *out=UINT64_C(1770000100)*UINT64_C(1000000000);
    return true;
}
uint64_t nas_arch_monotonic_ns(void) {
    return UINT64_C(9876543210);
}
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
    assert(node.atime_sec==1770000100 && node.mtime_sec==1770000100 &&
           node.ctime_sec==1770000100);
    assert(dir.mtime_sec==1770000100);
    assert(nas_vfs_chmod(&node,06755)==0);
    assert(nas_vfs_chown(&node,1001,2002)==0);
    struct nas_fs_node owner;
    assert(nas_vfs_getattr(&node,&owner)==0);
    assert(owner.uid==1001 && owner.gid==2002 &&
           (owner.mode&06000U)==0 && owner.ctime_sec>=1770000100);
    assert(nas_vfs_chown(&node,UINT32_MAX,3003)==0);
    assert(nas_fs_lookup_node("/ramfs/parent/file",true,&owner,path)==0);
    assert(owner.uid==1001 && owner.gid==3003);
    assert(nas_vfs_chown(&owner,4004,UINT32_MAX)==0);
    assert(nas_vfs_getattr(&node,&owner)==0);
    assert(owner.uid==4004 && owner.gid==3003 &&
           owner.ctime_sec==1770000100 && owner.ctime_nsec>0);
    /* The common VFS parses UTIME_NOW and UTIME_OMIT; the provider owns
     * exact ns timestamps. Both OMIT must be a true no-op (including ctime). */
    uint64_t atime=UINT64_C(1337000123),mtime=UINT64_C(1444000456);
    assert(nas_vfs_utimens(&node,atime,mtime,true,true)==0);
    assert(nas_vfs_getattr(&node,&owner)==0);
    assert(owner.atime_sec==atime/1000000000ULL &&
           owner.atime_nsec==atime%1000000000ULL &&
           owner.mtime_sec==mtime/1000000000ULL &&
           owner.mtime_nsec==mtime%1000000000ULL);
    uint64_t ctime_after=(owner.ctime_sec*1000000000ULL)+owner.ctime_nsec;
    assert(nas_vfs_utimens(&node,0,0,false,false)==0);
    assert(nas_vfs_getattr(&node,&owner)==0);
    assert(owner.atime_sec==atime/1000000000ULL &&
           owner.mtime_sec==mtime/1000000000ULL &&
           owner.ctime_sec*1000000000ULL+owner.ctime_nsec==ctime_after);
    assert(nas_vfs_utimens(&node,0,UINT64_C(1655000789),false,true)==0);
    assert(nas_vfs_getattr(&node,&owner)==0);
    assert(owner.atime_sec==atime/1000000000ULL &&
           owner.mtime_sec==1 && owner.mtime_nsec==655000789);
    /* Independent mounted vendor instances never share one identity. */
    assert(other.uid==0 && other.gid==0);
    assert(nas_vfs_node_pin(&node)==0);
    assert(nas_vfs_write_at(&node,0,(const uint8_t *)"hello",5)==5);
    assert(nas_vfs_node_size(&node)==5);
    assert(nas_vfs_getattr(&node,&owner)==0);
    assert(owner.mtime_sec>=1770000100 && owner.ctime_sec>=1770000100);
    assert(nas_fs_lookup_node("/ramfs/parent",true,&dir,path)==0);
    assert(nas_vfs_node_pin(&dir)==0);
    struct nas_vfs_path renamed_dir={0};
    assert(nas_vfs_path_bind(&dir,"/ramfs/parent",&renamed_dir)==0);
    assert(nas_vfs_rename("/ramfs/parent","/ramfs/moved",0)==0);
    assert(nas_vfs_path_string(&renamed_dir,path)==0);
    assert(!strcmp(path,"/ramfs/moved"));
    assert(nas_vfs_read_at(&node,0,(uint8_t *)text,5)==5);
    assert(!memcmp(text,"hello",5));
    /* A deliberately old mtime on the still-open old generation must not
     * bleed into a future inode when the name is reused. */
    assert(nas_vfs_utimens(&node,UINT64_C(1000000000),
                          UINT64_C(2000000000),true,true)==0);
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
    assert(renewed.mtime_sec==1770000100 && renewed.atime_sec==1770000100);
    assert(nas_vfs_write_at(&renewed,0,(const uint8_t *)"new",3)==3);
    assert(nas_vfs_read_at(&node,0,(uint8_t *)text,5)==5);
    assert(!memcmp(text,"hello",5));
    assert(nas_vfs_unlink("/ramfs/moved/file",0)==0);
    assert(nas_vfs_unlink("/ramfs/moved",0x200ULL)==0);
    assert(nas_vfs_getattr(&dir,&removed)==0 && removed.nlink==0);
    assert(nas_vfs_path_string(&renamed_dir,path)==-ENOENT);
    assert(nas_vfs_path_put(&renamed_dir)==0);
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
    struct nas_vfs_path unlinked_dir={0};
    assert(nas_vfs_path_bind(&dir,"/ramfs/open-dir",&unlinked_dir)==0);
    assert(nas_vfs_unlink("/ramfs/open-dir",0x200ULL)==0);
    assert(nas_vfs_getattr(&dir,&removed)==0 && removed.nlink==0);
    assert(nas_vfs_path_string(&unlinked_dir,path)==-ENOENT);
    assert(nas_vfs_path_put(&unlinked_dir)==0);
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
    /* Absent real RTC still uses the architecture's actual monotonic
     * clock; newly created upstream RamFS files must never report epoch 0. */
    fake_realtime_available=false;
    assert(nas_vfs_create("/ramfs-two/monotonic-file",0644)==0);
    assert(nas_fs_lookup_node("/ramfs-two/monotonic-file",true,&owner,path)==0);
    assert(owner.mtime_sec==9 && owner.mtime_nsec==876543210);
    fake_realtime_available=true;
    assert(nas_vfs_unlink("/ramfs-two/monotonic-file",0)==0);
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
    /* Pin ALL 320 independent third-party original RamFS inodes at once.
     * This invokes actual provider validate_inode/inode graph and shared
     * VFS lease ownership: previously the 256 global leases were a hard
     * architectural ceiling independent of available memory. */
    struct nas_fs_node live[320];
    for(unsigned i=0;i<320;i++) {
        char name[256];
        assert(snprintf(name,sizeof(name),"/ramfs/bulk/f%03u",i)>0);
        assert(nas_fs_lookup_node(name,true,&live[i],path)==0);
        assert(nas_vfs_node_pin(&live[i])==0);
        assert(nas_vfs_node_has_lease(&live[i]));
    }
    struct nas_fs_node attrs;
    for(unsigned i=0;i<320;i++) {
        assert(nas_vfs_getattr(&live[i],&attrs)==0);
        assert(attrs.ino==live[i].ino);
        assert(nas_vfs_node_unpin(&live[i])==0);
    }
    assert(!nas_vfs_node_has_lease(&live[319]));
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
    /* A 255-byte VFS path allows ~127 single-letter components. The
     * RamFS identity-to-path conversion must never recurse once per level:
     * that previously spent >32 KiB stack inside a 16 KiB kernel stack. */
    char deep[256]="/ramfs";
    for(unsigned i=0;i<124;i++) {
        size_t n=strlen(deep);
        assert(n+3<=sizeof(deep));
        deep[n]='/';deep[n+1]='d';deep[n+2]=0;
        assert(nas_vfs_mkdir(deep,0700)==0);
        assert(nas_fs_lookup_node(deep,true,&node,path)==0);
        assert(nas_vfs_getattr(&node,&attrs)==0 && attrs.is_dir);
        if(i==122) {
            char leaf[256];
            assert(snprintf(leaf,sizeof(leaf),"%s/f",deep)==254);
            assert(nas_vfs_create(leaf,0600)==0);
            assert(nas_fs_lookup_node(leaf,true,&node,path)==0);
            assert(nas_vfs_write_at(&node,0,(const uint8_t *)"deep",4)==4);
            assert(nas_vfs_read_at(&node,0,(uint8_t *)text,4)==4);
            assert(!memcmp(text,"deep",4));
            assert(nas_vfs_unlink(leaf,0)==0);
        }
    }
    assert(strlen(deep)==254);
    for(unsigned i=0;i<124;i++) {
        assert(nas_vfs_unlink(deep,0x200ULL)==0);
        deep[strlen(deep)-2]=0;
    }
    assert(!strcmp(deep,"/ramfs"));
    assert(nas_ramfs_vfs_unmount(&a)==0);
    assert(nas_ramfs_vfs_unmount(&b)==0);
    puts("[UPSTREAM RAMFS VFS PASS] upstream unchanged; VFS open-unlink orphan; old/new generations; open rmdir; 320+ identities; 700 reuse; 124-level paths; modes; multi-mount");
    return 0;
}
