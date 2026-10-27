/* Link real file_io.c and poll_syscall.c, not simulated readiness logic.
 * A subscription belongs to the Task, and the backend is selected by
 * the OPEN FILE DESCRIPTION. Both masks cover slots above 64. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/task/wait.h"
#include "../kernel/fs/file_ops.h"
struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];
static unsigned calls[128];
static uint16_t state[128];
static uint16_t ready(unsigned slot,uint16_t interests) {
    assert(slot<NAS_OPEN_FILE_LIMIT);
    calls[slot]++;
    return state[slot] & (interests | 0x18U);
}
/* Included only for the actual nas_fs_poll_ready's legacy TTY branch. */
bool nas_tty_poll_readable(void) {return false;}
extern bool nas_fs_poll_ready(const struct nas_task *);
int main(void) {
    static const struct nas_file_ops ops={.poll_description=ready};
    struct nas_task task={0}, child={0};
    nas_open_files[3].refcount=1;nas_open_files[3].ops=&ops;
    nas_open_files[126].refcount=2;nas_open_files[126].ops=&ops;
    nas_wait_io_arm(&task,3,1,false);
    nas_wait_io_arm(&task,126,4,false);
    assert(nas_wait_io_has_interest(&task));
    assert(nas_slot_mask_test(&task.wait.poll_interest_read,3));
    assert(nas_slot_mask_test(&task.wait.poll_interest_write,126));
    assert(!nas_fs_poll_ready(&task));
    state[126]=4;
    assert(nas_fs_poll_ready(&task) && calls[126]);
    state[126]=0;state[3]=0x10;
    assert(nas_fs_poll_ready(&task)); /* pipe/socket HUP wakes poll */
    state[3]=0;
    /* Previously EVENTFD lacked a backend poll_arm callback: the generic
     * wait-ownership layer arms ANY live description using the same code. */
    nas_wait_io_arm(&task,126,1,false);
    state[126]=1;
    assert(nas_fs_poll_ready(&task));
    /* Task fork must never inherit parent's in-flight interests. */
    child=task;
    nas_task_wait_reset(&child);
    assert(!nas_wait_io_has_interest(&child));
    assert(!nas_fs_poll_ready(&child));
    nas_wait_io_clear(&task);
    assert(!nas_wait_io_has_interest(&task) && !nas_fs_poll_ready(&task));
    nas_wait_io_arm(&task,128,1,false);
    assert(!nas_wait_io_has_interest(&task));
    nas_wait_io_arm(&task,3,1,true);
    assert(task.wait.poll_tty_read);
    nas_wait_io_clear(&task);
    assert(!task.wait.poll_tty_read);
    puts("[M90 IO WAIT PASS] description-agnostic poll, HUP, >64 slots, fork/reset, multi-interest, tty");
    return 0;
}
