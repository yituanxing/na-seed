/* Actual kernel/fs/file_io.c owner-independent description readiness.
 * No Task or userspace address participates in these callback results. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/fd.h"
#include "../kernel/fs/file_ops.h"
struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];
static struct nas_task issuing_task,other_task;
struct nas_task *nas_current_task=&issuing_task;
static unsigned observed_slot;
static uint16_t test_poll(unsigned slot,uint16_t interests) {
    observed_slot=slot;
    return interests&0x0004U;
}
static uint16_t task_poll(unsigned slot,uint16_t interests,
                          const struct nas_task *task) {
    observed_slot=slot;
    return task==&issuing_task ? (interests&1U):0;
}
int main(void) {
    static const struct nas_file_ops ops={.poll_description=test_poll};
    static const struct nas_file_ops task_ops={
        .poll_description=test_poll,.poll_description_task=task_poll
    };
    assert(nas_fs_poll_description(0,5)==0);
    nas_open_files[3].refcount=2; /* shared by distinct fork/dup FDs */
    nas_open_files[3].ops=&ops;
    assert(nas_fs_poll_description(3,5)==4 && observed_slot==3);
    assert(nas_fs_poll_description(3,1)==0 && observed_slot==3);
    nas_open_files[3].ops=&task_ops;
    assert(nas_fs_poll_description(3,5)==1 && observed_slot==3);
    assert(nas_fs_poll_description_task(3,5,&other_task)==0);
    nas_open_files[3].ops=&ops;
    assert(nas_fs_poll_description(128,5)==0);
    nas_open_files[3].ops=0;
    assert(nas_fs_poll_description(3,5)==0);
    nas_open_files[3].ops=&ops;
    nas_open_files[3].refcount=0;
    assert(nas_fs_poll_description(3,5)==0); /* final close */
    puts("[M89 POLL PASS] shared description, checked bounds, final close, backend readiness");
    return 0;
}
