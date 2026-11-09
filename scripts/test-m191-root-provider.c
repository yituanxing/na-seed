/* Real VFS source linked once per process. Exercise the explicitly selected
 * external boot root before ANY legacy bootfs mount or pseudo Dentry exists.
 * Reuse the established host provider fixture without running its main(). */
#define main nas_legacy_provider_main
/* Historical VFS path-only fixture intentionally rejects runtime tmpfs
 * inode allocation. This *separate* M191 root-lifecycle test supplies one
 * valid synthetic runtime inode; VFS and provider mounting remain real. */
#define tmp_inode_alloc_quota nas_m191_unused_path_only_alloc
#include "test-vfs-provider.c"
#undef tmp_inode_alloc_quota
#undef main

static struct nas_tmp_file alpine_runtime_root;
struct nas_tmp_file *tmp_inode_alloc_quota(bool dir,uint32_t mode,
                                          struct nas_tmp_quota *quota) {
    assert(dir && quota && !alpine_runtime_root.used);
    alpine_runtime_root=(struct nas_tmp_file){
        .used=true,.is_dir=true,.mode=0040000U|(mode&07777U),.ino=8502
    };
    return &alpine_runtime_root;
}

/* The original Alpine external root actually has /run. A legitimate OpenRC
 * mount(2) of runtime tmpfs there keeps TWO root Dentry leases (parent and
 * covered) and ONE child attachment; its explicit shutdown must release
 * all three before physical ext4 can unmount. */
static int alpine_root_child(const struct nas_fs_node *parent,
                             const char *name,struct nas_fs_node *out) {
    if(!strcmp(name,"run")) {
        *out=(struct nas_fs_node){.ino=803,.mode=0040755U,.is_dir=true};
        return 0;
    }
    return ext_mock_child(parent,name,out);
}
static const struct nas_vfs_type alpine_root_type={
    .name="ext-mount-mock",.root=ext_mock_root,
    .lookup_child=alpine_root_child,.read_only=true
};

int main(void) {
    const struct nas_vfs_mount root={
        .mountpoint="/",.type=&alpine_root_type,.source_label="m191-root"
    };
    assert(nas_vfs_prepare_external_boot_root()==0);
    const struct nas_vfs_mount out_of_order={
        .mountpoint="/dev",.type=&ext_mock_type
    };
    assert(nas_vfs_register_mount(&out_of_order)==-EBUSY);
    assert(nas_vfs_register_mount(&root)==0);
    assert(nas_vfs_prepare_external_boot_root()==-EBUSY);
    struct nas_fs_node node={0};
    char resolved[256];
    assert(nas_fs_lookup_node("/",true,&node,resolved)==0);
    assert(!strcmp(node.filesystem->name,"ext-mount-mock"));
    assert(nas_fs_lookup_node("/hello",true,&node,resolved)==0);
    assert(node.ino==802);
    assert(nas_fs_lookup_node("/dev",true,&node,resolved)==0);
    assert(!strcmp(node.filesystem->name,"devfs"));
    assert(nas_fs_lookup_node("/tmp",true,&node,resolved)==0);
    assert(!strcmp(node.filesystem->name,"tmpfs"));
    assert(nas_vfs_mount_tmpfs("/run","tmpfs",0755,false,128,256,false)==0);
    assert(nas_fs_lookup_node("/run",true,&node,resolved)==0);
    assert(!strcmp(node.filesystem->name,"tmpfs"));
    assert(nas_vfs_unregister_mount(&root)==-EBUSY);
    assert(nas_vfs_shutdown_external_root_pseudos()==0);
    /* Fixed /dev,/tmp,/proc,/sys have gone, but /run still pins root. */
    assert(nas_vfs_unregister_mount(&root)==-EBUSY);
    assert(nas_vfs_shutdown_runtime_tmpfs()==0);
    assert(nas_vfs_shutdown_runtime_tmpfs()==0); /* idempotent cleanup */
    assert(nas_vfs_unregister_mount(&root)==0);
    assert(nas_vfs_lookup("/",&node)==-ENOENT);
    fprintf(stderr,"[M191 EXTERNAL ROOT VFS PASS] root-first ownership, "
                   "real OpenRC runtime /run tmpfs + pseudo teardown, "
                   "clean physical ext4 detach\n");
    return 0;
}
