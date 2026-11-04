/* Real VFS source linked once per process. Exercise the explicitly selected
 * external boot root before ANY legacy bootfs mount or pseudo Dentry exists.
 * Reuse the established host provider fixture without running its main(). */
#define main nas_legacy_provider_main
#include "test-vfs-provider.c"
#undef main

int main(void) {
    const struct nas_vfs_mount root={
        .mountpoint="/",.type=&ext_mock_type,.source_label="m191-root"
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
    assert(nas_vfs_unregister_mount(&root)==-EBUSY);
    assert(nas_vfs_shutdown_external_root_pseudos()==0);
    assert(nas_vfs_unregister_mount(&root)==0);
    assert(nas_vfs_lookup("/",&node)==-ENOENT);
    fprintf(stderr,"[M191 EXTERNAL ROOT VFS PASS] root-first ownership, "
                   "real pseudo parents, clean detach\n");
    return 0;
}
