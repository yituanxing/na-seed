/* Reuse the actual VFS provider fixture, then exercise the Linux PATH_MAX
 * boundary through production namei and namespace code.  The fixture is
 * intentionally not duplicated: both executables run identical providers. */
#define main nas_vfs_provider_fixture_main
#include "test-vfs-provider.c"
#undef main

/* An actual registered VFS provider with inode identities per component.
 * It lets the production namei/Dentry code walk a 4095-byte directory and
 * a separate >256-component directory without a fake Path or a test-only
 * public Dentry constructor. The provider never consumes a full path. */
#define DEEP_NODES 320U
static uint8_t deep_inodes[DEEP_NODES];
static bool deep_many_components;
static int deep_root(const struct nas_vfs_mount *mount,
                     struct nas_fs_node *out) {
    (void)mount;
    *out=(struct nas_fs_node){.inode=&deep_inodes[0],.ino=60000,
                              .mode=0040755U,.nlink=1,.is_dir=true};
    return 0;
}
static int deep_child(const struct nas_fs_node *parent,const char *name,
                      struct nas_fs_node *out) {
    if(!parent || !parent->inode || !parent->is_dir)return -ENOTDIR;
    const uint8_t *entry=parent->inode;
    if(entry<deep_inodes || entry>=deep_inodes+DEEP_NODES)return -ESTALE;
    unsigned depth=(unsigned)(entry-deep_inodes);
    if(depth+1>=DEEP_NODES)return -ENOENT;
    if(deep_many_components) {
        if(depth>=300 || strcmp(name,"z"))return -ENOENT;
    } else {
        unsigned length=depth<20?200U:60U;
        char expected=depth<20?'x':'y';
        if(depth>=21 || strlen(name)!=length)return -ENOENT;
        for(unsigned i=0;i<length;i++)if(name[i]!=expected)return -ENOENT;
    }
    *out=(struct nas_fs_node){.inode=&deep_inodes[depth+1],
                              .ino=60001U+depth,.mode=0040755U,
                              .nlink=1,.is_dir=true};
    return 0;
}
static const struct nas_vfs_type deep_type={
    .name="deep-path-fixture",.root=deep_root,
    .lookup_child=deep_child,.read_only=true
};
static const struct nas_vfs_mount deep_mount={
    .mountpoint="/tmp/long-root",.type=&deep_type,
    .allow_missing_mountpoint=true
};
static void check_live_path_projection(void) {
    assert(nas_vfs_register_mount(&deep_mount)==0);
    char pathname[NAS_VFS_WALK_PATH_LIMIT+1];
    size_t length=strlen(deep_mount.mountpoint);
    memcpy(pathname,deep_mount.mountpoint,length);
    for(unsigned depth=0;depth<21;depth++) {
        unsigned n=depth<20?200U:60U;
        pathname[length++]='/';
        memset(pathname+length,depth<20?'x':'y',n);
        length+=n;
    }
    pathname[length]=0;
    assert(length==NAS_VFS_WALK_PATH_LIMIT-1);
    struct nas_vfs_path owned={0};
    assert(nas_fs_lookup_path_at(NULL,NULL,pathname,true,&owned)==0);
    char output[NAS_VFS_WALK_PATH_LIMIT+4];
    memset(output,0x5a,sizeof(output));
    /* A too-small destination must remain completely untouched. */
    assert(nas_vfs_path_string_buf(&owned,output,length)==-ENAMETOOLONG);
    assert(output[0]=='Z' && output[length]=='Z');
    assert(nas_vfs_path_string(&owned,output)==-ENAMETOOLONG);
    assert(output[0]=='Z');
    assert(nas_vfs_path_string_buf(&owned,output,length+1)==0);
    assert(!strcmp(output,pathname));
    assert(output[length+1]=='Z');
    assert(nas_vfs_path_put(&owned)==0);
    assert(nas_vfs_unregister_mount(&deep_mount)==0);

    /* More than 256 Dentry ancestors may still be a short valid pathname.
     * Old projection used a fixed array of 256 Dentry pointers. */
    deep_many_components=true;
    assert(nas_vfs_register_mount(&deep_mount)==0);
    length=strlen(deep_mount.mountpoint);
    memcpy(pathname,deep_mount.mountpoint,length);
    for(unsigned i=0;i<300;i++) {
        pathname[length++]='/';pathname[length++]='z';
    }
    pathname[length]=0;
    assert(nas_fs_lookup_path_at(NULL,NULL,pathname,true,&owned)==0);
    assert(nas_vfs_path_string_buf(&owned,output,sizeof(output))==0);
    assert(!strcmp(output,pathname));
    assert(nas_vfs_path_put(&owned)==0);
    assert(nas_vfs_unregister_mount(&deep_mount)==0);
}

#ifndef NAS_PATH_CONTRACT_MAIN_NAME
#define NAS_PATH_CONTRACT_MAIN_NAME main
#endif
int NAS_PATH_CONTRACT_MAIN_NAME(void) {
    assert(nas_vfs_provider_fixture_main()==0);
    check_live_path_projection();

    /* 4095 bytes (plus NUL) are admissible to namei; repeated dot
     * components must not force a 256-byte canonicalization roundtrip. */
    char long_name[NAS_VFS_WALK_PATH_LIMIT+2]={0};
    memcpy(long_name,"/tmp",5);
    unsigned at=4;
    while(at+2<NAS_VFS_WALK_PATH_LIMIT) {
        long_name[at++]='/';
        long_name[at++]='.';
    }
    assert(at==NAS_VFS_WALK_PATH_LIMIT-2);
    long_name[at++]='/';
    long_name[at]=0;
    assert(at==NAS_VFS_WALK_PATH_LIMIT-1);
    struct nas_vfs_path owned={0};
    assert(nas_fs_lookup_path_at(NULL,NULL,long_name,true,&owned)==0);
    struct nas_fs_node node={0};
    assert(nas_vfs_path_node(&owned,&node)==0 && node.is_dir);
    assert(nas_vfs_path_put(&owned)==0);
    char canonical[256]={0};
    assert(nas_fs_lookup_node(long_name,true,&node,canonical)==0);
    assert(!strcmp(canonical,"/tmp"));

    /* 4096 bytes are NOT a valid Linux pathname, even if all components
     * consist of slash separators and the root fast-path would match. */
    long_name[at++]='/';
    long_name[at]=0;
    assert(at==NAS_VFS_WALK_PATH_LIMIT);
    assert(nas_fs_lookup_path_at(NULL,NULL,long_name,true,&owned)
           ==-ENAMETOOLONG);

    char roots[NAS_VFS_WALK_PATH_LIMIT+2];
    memset(roots,'/',NAS_VFS_WALK_PATH_LIMIT+1);
    roots[NAS_VFS_WALK_PATH_LIMIT-1]=0;
    assert(nas_fs_root_only(roots));
    roots[NAS_VFS_WALK_PATH_LIMIT-1]='/';
    roots[NAS_VFS_WALK_PATH_LIMIT]=0;
    assert(!nas_fs_root_only(roots));
    assert(nas_vfs_mkdir(roots,0755)==-ENAMETOOLONG);
    assert(nas_vfs_unlink(roots,0)==-ENAMETOOLONG);
    assert(nas_vfs_create(roots,0644)==-ENAMETOOLONG);

    puts("[PATH CONTRACT PASS] 4095-byte Path-first lookup and display, "
         ">256 Dentry ancestors, exact PATH_MAX rejection");
    return 0;
}
