#ifndef NASEED_FS_PROCFS_H
#define NASEED_FS_PROCFS_H
#include <stdint.h>
#include <stdbool.h>
/* Kernel Task/VM statistics exposed through a read-only, volatile namespace.
 * No private FD table or program-name special cases. */
enum nas_proc_kind {
    NAS_PROC_NONE=0, NAS_PROC_ROOT, NAS_PROC_PID_DIR,
    NAS_PROC_MEMINFO, NAS_PROC_UPTIME, NAS_PROC_VERSION, NAS_PROC_CPUINFO,
    NAS_PROC_PID_STAT, NAS_PROC_PID_STATUS, NAS_PROC_PID_COMM,
    NAS_PROC_PID_CMDLINE, NAS_PROC_STAT, NAS_PROC_LOADAVG,
    NAS_PROC_VMSTAT, NAS_PROC_PID_STATM,
    NAS_PROC_TASK_DIR, NAS_PROC_TID_DIR,
    NAS_PROC_FD_DIR, NAS_PROC_FDINFO_DIR,
    NAS_PROC_FD_LINK, NAS_PROC_FDINFO_FILE,
    NAS_PROC_PID_MAPS,
    NAS_PROC_MOUNTS
};
/* pid identifies the actual TID for task/<tid>; tgid records the
 * parent process directory. The same node is owned by the shared FD.
 * Neither a second task registry nor a per-app synthetic /proc overlay. */
struct nas_proc_node {uint8_t kind;uint64_t pid,tgid;uint16_t fd;};
bool nas_proc_lookup(const char *path,struct nas_proc_node *out);
/* Component lookup from stable procfs directory inode identity. */
bool nas_proc_find_child(uint64_t dir_ino,const char *name,struct nas_proc_node *out);
bool nas_proc_is_dir(struct nas_proc_node node);
uint32_t nas_proc_mode(struct nas_proc_node node);
bool nas_proc_dir_path(struct nas_proc_node node,char path[256]);
uint64_t nas_proc_ino(struct nas_proc_node node);
/* Dynamic fd symlink target for readlink/readlinkat. No copied FD table. */
int64_t nas_proc_readlink(struct nas_proc_node node,char *target,unsigned cap);
bool nas_proc_child(struct nas_proc_node directory,unsigned index,
                    char name[32],struct nas_proc_node *node);
int64_t nas_proc_read(struct nas_proc_node node,uint64_t offset,
                      uint8_t *output,unsigned size);
#endif
