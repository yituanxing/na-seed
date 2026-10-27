#include "lookup.h"
#include "../bootfs.h"
#include "../errno.h"
/* Generic namei over mounted filesystem callbacks. Retains existing bounded
 * path/symlink policy, but never calls any concrete filesystem directly. */
int nas_fs_resolve_existing(const char *path,bool follow_final,char out[256]) {
    if(!bootfs_mounted() || !path || path[0]!='/')return -ENOENT;
    char pending[256];unsigned length=0;
    while(path[length]) {
        if(length>=255)return -ENAMETOOLONG;
        pending[length]=path[length];length++;
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
                resolved[used]=0;continue;
            }
            if(used+(used!=1)+len>=256)return -ENAMETOOLONG;
            if(used!=1)resolved[used++]='/';
            for(unsigned i=0;i<len;i++)resolved[used++]=pending[start+i];
            resolved[used]=0;
            struct nas_fs_node inode;
            int rc=nas_vfs_lookup(resolved,&inode);
            if(rc)return rc;
            unsigned tail=pos;
            while(pending[tail]=='/')tail++;
            bool last=!pending[tail];
            if(inode.is_symlink && (!last || follow_final)) {
                if(++links>8)return -ELOOP;
                char expanded[256],target[256];
                int nr=nas_vfs_readlink(&inode,target);
                if(nr<0)return nr;
                unsigned size=0;
                if(target[0]!='/') {
                    unsigned parent=used-len;
                    if(parent>1 && resolved[parent-1]=='/')parent--;
                    for(unsigned i=0;i<parent;i++)expanded[size++]=resolved[i];
                    if(size>1)expanded[size++]='/';
                }
                if(size+(unsigned)nr>=256)return -ENAMETOOLONG;
                for(unsigned i=0;i<(unsigned)nr;i++)expanded[size++]=target[i];
                for(unsigned i=pos;pending[i];i++) {
                    if(size>=255)return -ENAMETOOLONG;
                    expanded[size++]=pending[i];
                }
                expanded[size]=0;
                if(expanded[0]!='/')return -ENOENT;
                for(unsigned i=0;i<=size;i++)pending[i]=expanded[i];
                restart=true;break;
            }
            if(!last && !inode.is_dir)return -ENOTDIR;
        }
        if(restart)continue;
        for(unsigned i=0;i<=used;i++)out[i]=resolved[i];
        return 0;
    }
}
int nas_fs_lookup_node(const char *path,bool follow_final,
                       struct nas_fs_node *result,char resolved[256]) {
    if(!path || !result || !resolved || !bootfs_mounted())return -ENOENT;
    if(path[0]!='/')return -EINVAL;
    int err=nas_fs_resolve_existing(path,follow_final,resolved);
    return err ? err : nas_vfs_lookup(resolved,result);
}
