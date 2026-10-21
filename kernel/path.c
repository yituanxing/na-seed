#include "path.h"
#include "bootfs.h"
#include "errno.h"
#include <stdbool.h>

/* NaOS-inspired small seam: pathwalk is independent of userspace copying
 * and descriptor lifetime; no invented dentry/mount cache or general VFS.
 * For CPIO, check component order before a lexical '..' can erase a real
 * symbolic link (e.g. /bin/cat/../busybox must return ENOTDIR).
 * The bounded volatile /tmp backend still uses lexical normalization.
 */
static bool dotdot_component(const char *s) {
    for (unsigned i=0;s[i];i++) {
        if ((i==0 || s[i-1]=='/') && s[i]=='.' && s[i+1]=='.' &&
            (!s[i+2] || s[i+2]=='/')) return true;
    }
    return false;
}
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
static bool on_cpio_branch(const char *raw) {
    /* A known archive root means an error inside it cannot be silently
     * discarded by a lexical '..'. /tmp is a different writable backend.
     */
    if (raw[0]!='/' || !raw[1]) return false;
    char first[256];
    unsigned n=0;
    do { first[n]=raw[n]; n++; } while (raw[n] && raw[n]!='/');
    first[n]=0;
    return bootfs_find(first) && !(first[0]=='/' && first[1]=='t' &&
        first[2]=='m' && first[3]=='p' && first[4]==0);
}
int nas_path_resolve(const char *base, const char *src, char out[256]) {
    if (!src || !src[0]) return -ENOENT;
    /* Run the real CPIO symlink walk while '..' remains in input.
     * Existing paths are canonicalized; final symlink stays untouched so
     * lstat/readlink/O_NOFOLLOW keep their correct final-component policy.
     */
    if (bootfs_mounted() && dotdot_component(src)) {
        char raw[256];
        if (path_from_base(base,src,raw)) {
            int rc=bootfs_resolve(raw,false,out);
            if (!rc) return 0;
            if (rc!=-ENOENT || on_cpio_branch(raw)) return rc;
        }
    }
    unsigned used=1;
    out[0]='/';
    if (src[0]!='/') {
        if (!base || base[0]!='/') return -EINVAL;
        for (unsigned i=1;base[i];i++) {
            if (used+1>=256) return -ENAMETOOLONG;
            out[used++]=base[i];
        }
    }
    for (unsigned pos=0;src[pos];) {
        while (src[pos]=='/') pos++;
        unsigned start=pos;
        while (src[pos] && src[pos]!='/') pos++;
        unsigned len=pos-start;
        if (!len || (len==1 && src[start]=='.')) continue;
        if (len==2 && src[start]=='.' && src[start+1]=='.') {
            if (used>1) {
                while (used>1 && out[used-1]!='/') used--;
                if (used>1) used--;
            }
            continue;
        }
        if (used+len+1>=256) return -ENAMETOOLONG;
        if (used!=1) out[used++]='/';
        for (unsigned i=0;i<len;i++) out[used++]=src[start+i];
    }
    out[used]=0;
    return 0;
}
