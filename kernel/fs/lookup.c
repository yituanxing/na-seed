#include "lookup.h"
#include "../errno.h"
#include "../lib/string.h"
/* A single VFS component pathwalk. The filesystem observes only a parent
 * inode and one name; the VFS alone handles symlinks, mountpoint crossings,
 * legacy overlay, dot/dotdot and canonical path construction. Owned
 * mount+dentry objects will replace the current bounded path cache later. */
static int resolve_walk(const char *path,bool follow_final,
                        struct nas_fs_node *result,char out[256]) {
    if(!path || path[0]!='/')return -ENOENT;
    char pending[256];unsigned length=0;
    while(path[length]) {
        if(length>=255)return -ENAMETOOLONG;
        pending[length]=path[length];length++;
    }
    pending[length]=0;
    const bool trailing_slash=length>1 && pending[length-1]=='/';
    unsigned links=0;
    for(;;) {
        char resolved[256];
        resolved[0]='/';resolved[1]=0;
        unsigned used=1,pos=0;
        struct nas_fs_node current={0};
        bool have_current=nas_vfs_lookup("/", &current)==0;
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
                /* Until VFS owns parent dentries, moving upward must
                 * revalidate the current namespace identity. */
                have_current=nas_vfs_lookup(resolved,&current)==0;
                continue;
            }
            if(used+(used!=1)+len>=256)return -ENAMETOOLONG;
            if(used!=1)resolved[used++]='/';
            for(unsigned i=0;i<len;i++)resolved[used++]=pending[start+i];
            resolved[used]=0;
            char component[256];
            for(unsigned i=0;i<len;i++)component[i]=pending[start+i];
            component[len]=0;
            struct nas_fs_node inode;
            int rc=nas_vfs_lookup_step(have_current?&current:NULL,
                                       resolved,component,&inode);
            if(rc)return rc;
            unsigned tail=pos;
            while(pending[tail]=='/')tail++;
            bool last=!pending[tail];
            if(inode.is_symlink && (!last || follow_final || trailing_slash)) {
                if(++links>8)return -ELOOP;
                char expanded[256],target[256];
                int nr=nas_vfs_readlink(&inode,resolved,target);
                if(nr<0)return nr;
                if(!nr)return -ENOENT;
                if(nr>255)return -ENAMETOOLONG;
                for(int k=0;k<nr;k++)if(!target[k])return -EINVAL;
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
            if((!last || trailing_slash) && !inode.is_dir)return -ENOTDIR;
            current=inode;have_current=true;
        }
        if(restart)continue;
        for(unsigned i=0;i<=used;i++)out[i]=resolved[i];
        if(result) {
            if(!have_current)return -ENOENT;
            *result=current;
        }
        return 0;
    }
}
int nas_fs_resolve_existing(const char *path,bool follow_final,char out[256]) {
    if(!out)return -EINVAL;
    return resolve_walk(path,follow_final,NULL,out);
}
int nas_fs_lookup_node(const char *path,bool follow_final,
                       struct nas_fs_node *result,char resolved[256]) {
    if(!path || !result || !resolved)return -ENOENT;
    if(path[0]!='/')return -EINVAL;
    return resolve_walk(path,follow_final,result,resolved);
}
