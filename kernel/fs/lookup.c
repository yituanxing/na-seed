#include "lookup.h"
#include "../bootfs.h"
#include "../tmpfs.h"
#include "../errno.h"
#include "../lib/string.h"

/* This common VFS walk overlays real /tmp inodes on the immutable archive.
 * It is deliberately *not* linked into bootfs.c: standalone CPIO index
 * tests and early boot remain pure and independent from mutable tmpfs.
 * Preserve component order through symlink/.. expansion with an 8-link cap. */
int nas_fs_resolve_existing(const char *path,bool follow_final,char out[256]) {
    if(!bootfs_mounted() || !path || path[0]!='/')return -ENOENT;
    char pending[256];
    unsigned length=0;
    while(path[length]) {
        if(length>=255)return -ENAMETOOLONG;
        pending[length]=path[length];
        length++;
    }
    pending[length]=0;
    unsigned links=0;
    for(;;) {
        char resolved[256];
        resolved[0]='/';resolved[1]=0;
        unsigned used=1,pos=0;
        bool restart=false;
        while(pending[pos]) {
            while(pending[pos]=='/')pos++;
            if(!pending[pos])break;
            unsigned start=pos;
            while(pending[pos] && pending[pos]!='/')pos++;
            unsigned len=pos-start;
            if(len==1 && pending[start]=='.')continue;
            if(len==2 && pending[start]=='.' && pending[start+1]=='.') {
                while(used>1 && resolved[used-1]!='/')used--;
                if(used>1)used--;
                resolved[used]=0;
                continue;
            }
            if(used+(used!=1)+len>=256)return -ENAMETOOLONG;
            if(used!=1)resolved[used++]='/';
            for(unsigned i=0;i<len;i++)resolved[used++]=pending[start+i];
            resolved[used]=0;
            const struct bootfs_entry *entry=bootfs_find(resolved);
            const struct nas_tmp_file *tmp=entry?0:tmp_find(resolved);
            if(!entry && !tmp)return -ENOENT;
            unsigned tail=pos;
            while(pending[tail]=='/')tail++;
            bool last=!pending[tail];
            if((bootfs_is_link(entry) || tmp_is_link(tmp)) &&
               (!last || follow_final)) {
                if(++links>8)return -ELOOP;
                char expanded[256],target_buf[256];
                const uint8_t *target=entry?entry->data:(const uint8_t *)target_buf;
                uint64_t target_len=entry?entry->size:tmp->size;
                if(tmp) {
                    int err=tmp_link_target(tmp,target_buf);
                    if(err)return err;
                }
                unsigned size=0;
                if(target[0]!='/') {
                    unsigned parent=used-len;
                    if(parent>1 && resolved[parent-1]=='/')parent--;
                    for(unsigned i=0;i<parent;i++)expanded[size++]=resolved[i];
                    if(size>1)expanded[size++]='/';
                }
                if(size+target_len>=256)return -ENAMETOOLONG;
                for(uint64_t i=0;i<target_len;i++)expanded[size++]=(char)target[i];
                for(unsigned i=pos;pending[i];i++) {
                    if(size>=255)return -ENAMETOOLONG;
                    expanded[size++]=pending[i];
                }
                expanded[size]=0;
                if(expanded[0]!='/')return -ENOENT;
                for(unsigned i=0;i<=size;i++)pending[i]=expanded[i];
                restart=true;
                break;
            }
            if(!last && !(entry?bootfs_is_dir(entry):tmp->is_dir))return -ENOTDIR;
        }
        if(restart)continue;
        for(unsigned i=0;i<=used;i++)out[i]=resolved[i];
        return 0;
    }
}

/* Common lookup for exec and filesystem permissions. The existing CPIO
 * index and tmpfs inodes remain the sources of truth; no second ELF path
 * mapper or per-ISA file path is introduced. */
int nas_fs_lookup_node(const char *path, bool follow_final,
                       struct nas_fs_node *result, char resolved[256]) {
    if(!path || !result || !resolved || !bootfs_mounted())return -ENOENT;
    if(path[0]!='/')return -EINVAL;
    int err=nas_fs_resolve_existing(path,follow_final,resolved);
    if(err)return err;
    struct nas_tmp_file *tmp=tmp_find(resolved);
    if(tmp) {
        *result=(struct nas_fs_node){
            .backend=NAS_FS_TMPFS,.data=0,.tmp=tmp,.size=tmp->size,
            .mode=tmp->mode,.is_dir=tmp->is_dir
        };
        return 0;
    }
    const struct bootfs_entry *entry=bootfs_find(resolved);
    if(!entry)return -ENOENT;
    *result=(struct nas_fs_node){
        .backend=NAS_FS_BOOTFS,.data=entry->data,.size=entry->size,
        .mode=entry->mode,.is_dir=bootfs_is_dir(entry)
    };
    return 0;
}
