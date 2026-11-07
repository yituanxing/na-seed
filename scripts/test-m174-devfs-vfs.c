/* M174: production VFS/namei against the production devfs provider.
 * Reuse the established bootfs/tmpfs/VFS host fixture without copying any
 * fake pathname walker, mount registry or heap into a parallel test OS.
 */
#define main m174_existing_vfs_fixture_main
#include "test-vfs-provider.c"
#undef main
#include "../kernel/drivers/core/chrdev.h"

static bool m174_present=true;
static bool m174_is_present(unsigned unit) {
    return m174_present && (unit==174U || unit==175U || unit==176U);
}
static struct nas_chrdev_node m174_alpha={
    .path="/dev/m174_alpha",
    .info={.fd_kind=1,.unit=174,.dev=7,.ino=4174,
           .rdev=(250U<<8)|174U,.mode=0020660U},
    .present=m174_is_present
};
static struct nas_chrdev_node m174_input={
    .path="/dev/input/m174_event7",
    .info={.fd_kind=2,.unit=175,.dev=7,.ino=4175,
           .rdev=(13U<<8)|175U,.mode=0020600U},
    .present=m174_is_present
};
static struct nas_chrdev_node m174_pts={
    .path="/dev/pts/m174_slave",
    .info={.fd_kind=3,.unit=176,.dev=7,.ino=4176,
           .rdev=(136U<<8)|176U,.mode=0020620U},
    .present=m174_is_present
};

static struct nas_vfs_path m174_path(const char *name) {
    struct nas_vfs_path path={0};
    assert(nas_fs_lookup_path_at(NULL,NULL,name,true,&path)==0);
    assert(path.dentry);
    return path;
}
static struct nas_fs_node m174_node(const struct nas_vfs_path *path) {
    struct nas_fs_node node={0};
    assert(nas_vfs_path_node(path,&node)==0);
    assert(node.filesystem && !strcmp(node.filesystem->name,"devfs"));
    return node;
}
static bool m174_dir_has(const struct nas_fs_node *dir,const char *name,
                          uint8_t expected_type) {
    for(uint64_t cursor=0;cursor<80;cursor++) {
        struct nas_vfs_dirent entry={0};
        int found=nas_vfs_read_dir(dir,cursor,&entry,1);
        assert(found>=0);
        if(!found)return false;
        if(!strcmp(entry.name,name)) {
            assert(entry.dtype==expected_type && entry.ino);
            return true;
        }
    }
    assert(!"devfs getdents cursor never terminated");
    return false;
}
static void m174_open_and_release(const struct nas_vfs_path *dir,
                                  const char *name,
                                  struct nas_chrdev_node *device) {
    struct nas_vfs_open_options options={.follow_final=true,.access=2};
    struct nas_fs_node owned={0};
    struct nas_vfs_path path={0};
    assert(nas_vfs_open_path_at(dir,NULL,name,&options,&owned,&path)==0);
    assert((owned.mode&0170000U)==0020000U);
    assert(owned.rdev==device->info.rdev);
    assert(nas_chrdev_node_unregister(device)==-EBUSY);
    assert(nas_vfs_node_unpin(&owned)==0);
    assert(nas_vfs_path_put(&path)==0);
}
int main(void) {
    /* Missing mountpoint is created by VFS mount semantics, not a parallel
     * /dev special-case in the pathname resolver. */
    assert(nas_chrdev_node_register(&m174_alpha)==0);
    assert(nas_chrdev_node_register(&m174_input)==0);
    assert(nas_chrdev_node_register(&m174_pts)==0);
    struct nas_vfs_path dev=m174_path("/dev");
    struct nas_vfs_path input=m174_path("/dev/input");
    struct nas_vfs_path pts=m174_path("/dev/pts");
    struct nas_fs_node dev_ino=m174_node(&dev);
    struct nas_fs_node input_ino=m174_node(&input);
    struct nas_fs_node pts_ino=m174_node(&pts);
    assert(dev_ino.is_dir && input_ino.is_dir && pts_ino.is_dir);
    assert(m174_dir_has(&dev_ino,"input",4));
    assert(m174_dir_has(&dev_ino,"pts",4));
    assert(m174_dir_has(&dev_ino,"m174_alpha",2));
    assert(m174_dir_has(&input_ino,"m174_event7",2));
    assert(m174_dir_has(&pts_ino,"m174_slave",2));
    struct nas_vfs_path input_device=m174_path("/dev/input/m174_event7");
    struct nas_vfs_path pts_device=m174_path("/dev/pts/m174_slave");
    assert(m174_node(&input_device).rdev==m174_input.info.rdev);
    assert(m174_node(&pts_device).rdev==m174_pts.info.rdev);
    assert(nas_vfs_path_put(&input_device)==0);
    assert(nas_vfs_path_put(&pts_device)==0);

    struct nas_vfs_path alpha=m174_path("/dev/m174_alpha");
    struct nas_fs_node alpha_ino=m174_node(&alpha);
    assert(alpha_ino.rdev==m174_alpha.info.rdev &&
           (alpha_ino.mode&0170000U)==0020000U);
    const uint64_t old_ino=alpha_ino.ino;
    struct nas_vfs_path via_dot={0};
    assert(nas_fs_lookup_path_at(&dev,NULL,"./m174_alpha",true,
                                 &via_dot)==0);
    assert(m174_node(&via_dot).ino==old_ino);
    assert(nas_vfs_path_put(&via_dot)==0);
    assert(nas_fs_lookup_path_at(NULL,NULL,"/dev/m174_alpha/child",true,
                                 &via_dot)==-ENOTDIR);
    /* Reuse the independent VFS fixture's actual tmpfs symlink backend:
     * a link outside /dev must cross into devfs through shared namei. */
    const char *previous_link=fixture_link_target;
    uint64_t previous_length=fixture_symlink.size;
    fixture_link_target="/dev/m174_alpha";
    fixture_symlink.size=strlen(fixture_link_target);
    struct nas_vfs_path link=m174_path("/tmp/link");
    assert(m174_node(&link).ino==old_ino);
    assert(nas_vfs_path_put(&link)==0);
    fixture_link_target=previous_link;
    fixture_symlink.size=previous_length;
    m174_open_and_release(&dev,"m174_alpha",&m174_alpha);
    assert(nas_chrdev_node_unregister(&m174_alpha)==-EBUSY);
    assert(nas_vfs_path_put(&alpha)==0);

    /* O_PATH has inode identity but must not manufacture a driver handle. */
    struct nas_vfs_open_options path_only={.follow_final=true,.path_only=true};
    struct nas_fs_node owned={0};
    struct nas_vfs_path opened={0};
    assert(nas_vfs_open_path_at(&dev,NULL,"m174_alpha",&path_only,
                                &owned,&opened)==0);
    assert(owned.ino==old_ino && owned.rdev==m174_alpha.info.rdev);
    assert(nas_vfs_node_unpin(&owned)==0);
    assert(nas_vfs_path_put(&opened)==0);

    struct nas_vfs_open_options excl={.follow_final=true,.create=true,
                                     .exclusive=true};
    assert(nas_vfs_open_path_at(&dev,NULL,"m174_alpha",&excl,
                                &owned,&opened)==-EEXIST);
    struct nas_vfs_open_options trunc={.follow_final=true,
                                      .truncate=true,.access=2};
    assert(nas_vfs_open_path_at(&dev,NULL,"m174_alpha",&trunc,
                                &owned,&opened)==0);
    assert(nas_vfs_node_unpin(&owned)==0);
    assert(nas_vfs_path_put(&opened)==0);

    /* Disappearing hardware affects fresh discovery but does not turn an
     * already-owned Dentry into a replacement device's alias. */
    m174_present=false;
    assert(nas_fs_lookup_path_at(NULL,NULL,"/dev/m174_alpha",true,
                                 &opened)==-ENOENT);
    m174_present=true;
    assert(nas_chrdev_node_unregister(&m174_alpha)==0);
    assert(!m174_dir_has(&dev_ino,"m174_alpha",2));
    assert(nas_chrdev_node_register(&m174_alpha)==0);
    struct nas_vfs_path replaced=m174_path("/dev/m174_alpha");
    struct nas_fs_node replaced_ino=m174_node(&replaced);
    assert(replaced_ino.ino!=old_ino && replaced_ino.rdev==alpha_ino.rdev);
    assert(nas_vfs_path_put(&replaced)==0);
    assert(nas_vfs_path_put(&dev)==0);
    assert(nas_vfs_path_put(&input)==0);
    assert(nas_vfs_path_put(&pts)==0);
    assert(nas_chrdev_node_unregister(&m174_alpha)==0);
    assert(nas_chrdev_node_unregister(&m174_input)==0);
    assert(nas_chrdev_node_unregister(&m174_pts)==0);
    puts("[M174 DEVFS VFS PASS] mounted namei/readdir/dirfd/symlink, input/pts classes, rdev, O_RDWR/O_TRUNC/O_PATH/O_EXCL, stale path and reincarnation");
    return 0;
}
