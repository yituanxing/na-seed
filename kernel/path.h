#ifndef NASEED_PATH_H
#define NASEED_PATH_H
/* Preserve raw components. Shared namei owns dot/dotdot and symlinks. */
/* Join without lexical normalization: only namei may interpret dot,
 * dotdot and symlinks across VFS mounts. */
int nas_path_join_raw(const char *base,const char *src,char out[256]);
#endif
