/* Reuse the actual VFS provider fixture, then exercise the Linux PATH_MAX
 * boundary through production namei and namespace code.  The fixture is
 * intentionally not duplicated: both executables run identical providers. */
#define main nas_vfs_provider_fixture_main
#include "test-vfs-provider.c"
#undef main

int main(void) {
    assert(nas_vfs_provider_fixture_main()==0);

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

    puts("[PATH CONTRACT PASS] 4095-byte Path-first lookup and exact "
         "PATH_MAX rejection across namespace root fast paths");
    return 0;
}
