#ifndef NASEED_FD_H
#define NASEED_FD_H
#include <stdbool.h>
#include <stdint.h>

/* Per-task descriptor entry and global shared open-description lifetime.
 * Descriptor table fields stay in nas_task; offset/status and reference
 * identity are shared by fork and dup, not copied per descriptor.
 * A return value of NAS_FD_LIMIT means no final description was released.
 */
struct nas_task;
void nas_fd_system_init(void);
unsigned nas_fd_close(struct nas_task *task, unsigned fd);
uint64_t nas_fd_clone(struct nas_task *task, uint64_t src, uint64_t dst,
                      bool cloexec); /* caller must close dst beforehand */
uint64_t nas_fd_duplicate(struct nas_task *task, uint64_t src,
                          uint64_t minimum, bool cloexec);
bool nas_task_hold_files(const struct nas_task *task);

extern uint16_t nas_file_refcount[64];
extern uint64_t nas_file_status_flags[64];
extern uint64_t nas_boot_file_offsets[64];
#endif
