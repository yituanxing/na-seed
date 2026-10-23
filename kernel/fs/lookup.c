#include "lookup.h"
#include "../bootfs.h"
#include "../tmpfs.h"
#include "../errno.h"
#include "../lib/string.h"

/* Common lookup for exec and filesystem permissions. The existing CPIO
 * index and tmpfs inodes remain the sources of truth; no second ELF path
 * mapper or per-ISA file path is introduced. */
int nas_fs_lookup_node(const char *path, bool follow_final,
                       struct nas_fs_node *result, char resolved[256]) {
    if(!path || !result || !resolved || !bootfs_mounted())return -ENOENT;
    if(path[0]!='/')return -EINVAL;
    if(tmp_path(path)) {
        struct nas_tmp_file *tmp=tmp_find(path);
        if(tmp) {
            unsigned i=0;
            do { resolved[i]=path[i]; } while(path[i++]);
            *result=(struct nas_fs_node){
                .backend=NAS_FS_TMPFS,.data=tmp->bytes,.size=tmp->size,
                .mode=tmp->mode,.is_dir=tmp->is_dir
            };
            return 0;
        }
    }
    int err=bootfs_resolve(path,follow_final,resolved);
    if(err)return err;
    const struct bootfs_entry *entry=bootfs_find(resolved);
    if(!entry)return -ENOENT;
    *result=(struct nas_fs_node){
        .backend=NAS_FS_BOOTFS,.data=entry->data,.size=entry->size,
        .mode=entry->mode,.is_dir=bootfs_is_dir(entry)
    };
    return 0;
}
