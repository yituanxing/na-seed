#ifndef NASEED_TASK_WAIT_H
#define NASEED_TASK_WAIT_H
#include <stdbool.h>
#include <stdint.h>
#include "../task.h"
bool nas_task_wait_ready(const struct nas_task *task);
/* Per-Task interest registration, never per user-FD ownership. Tasks may
 * subscribe to multiple descriptions in a single poll/select/epoll.
 * Poll readiness is evaluated by each open-description's own callback. */
static inline void nas_wait_io_clear(struct nas_task *task) {
    if(!task)return;
    nas_slot_mask_clear(&task->wait.poll_pipe_read);
    nas_slot_mask_clear(&task->wait.poll_pipe_write);
    task->wait.poll_tty_read=false;
}
static inline void nas_wait_io_arm(struct nas_task *task,unsigned slot,
                                    uint16_t events,bool tty_read) {
    if(!task || slot>=NAS_OPEN_FILE_LIMIT)return;
    if(events&0x0001U)nas_slot_mask_set(&task->wait.poll_pipe_read,slot);
    if(events&0x0004U)nas_slot_mask_set(&task->wait.poll_pipe_write,slot);
    if((events&0x0001U)&&tty_read)task->wait.poll_tty_read=true;
}
static inline bool nas_wait_io_has_interest(const struct nas_task *task) {
    return task && (task->wait.poll_tty_read ||
           nas_slot_mask_any(&task->wait.poll_pipe_read) ||
           nas_slot_mask_any(&task->wait.poll_pipe_write));
}
/* Pure by-value reset: standalone task.c fixtures need no device/provider link.
 * The scheduler readiness provider remains a separate shared C unit. */
static inline void nas_task_wait_reset(struct nas_task *task) {
    if (task) task->wait=(struct nas_task_wait){0};
}
#endif
