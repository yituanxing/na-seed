#include "path.h"
#include "errno.h"
#include <stdbool.h>

/* Preserve raw components for VFS namei. No lexical dotdot scanner. */
static bool path_from_base(const char *base, const char *src,
                           char raw[256]) {
    unsigned n=0;
    if (src[0]!='/') {
        if (!base || base[0]!='/') return false;
        while (base[n]) {
            if (n>=254) return false;
            raw[n]=base[n];
            n++;
        }
        if (n>1 && raw[n-1]!='/') raw[n++]='/';
    }
    for (unsigned i=0;src[i];i++) {
        if (n>=255) return false;
        raw[n++]=src[i];
    }
    raw[n]=0;
    return true;
}
/* Raw join for the VFS component walker. Do not erase './..' before
 * namei: /regular-file/.. must be ENOTDIR, not a lexical parent. */
int nas_path_join_raw(const char *base,const char *src,char out[256]) {
    if(!src || !src[0] || !out)return -ENOENT;
    if(src[0]!='/' && (!base || base[0]!='/'))return -EINVAL;
    return path_from_base(base,src,out) ? 0 : -ENAMETOOLONG;
}
