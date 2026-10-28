#ifndef NASEED_PATH_H
#define NASEED_PATH_H
/* Kernel string path processing only: user copy and dirfd lifetime stay in
 * syscall layer. Absolute paths do not consult the supplied cwd.
 * Returns 0 or negative Linux errno; the output is always NUL-terminated
 * on success and is bounded to 256 bytes.
 */
int nas_path_resolve(const char *base, const char *src, char out[256]);
/* Join without lexical normalization: only namei may interpret dot,
 * dotdot and symlinks across VFS mounts. */
int nas_path_join_raw(const char *base,const char *src,char out[256]);
#endif
