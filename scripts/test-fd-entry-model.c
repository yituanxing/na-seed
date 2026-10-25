/* M62: test the REAL common fd.c object owner, not a reimplementation.
 * clang/cc host smoke checks fork/dup CLOEXEC vs description refs. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/task.h"
#include "../kernel/fd.h"
#include "../kernel/errno.h"

void nas_print(const char *s) { (void)s; }
void nas_dec(uint64_t n) { (void)n; }
/* fd.c owns placement; this test supplies a minimal inert ops provider. */
const struct nas_file_ops *nas_fs_ops_for_kind(uint8_t kind) {
    (void)kind; return 0;
}


int main(void) {
    _Static_assert(sizeof(struct nas_fd_entry) == 3, "stable small FD entry");
    _Static_assert(sizeof(struct nas_fd_table) == NAS_FD_LIMIT * 3,
                   "FD table contains no parallel arrays");
    static struct nas_task p, child, sibling;
    p.fds = &p.fds_owned;
    child.fds = &child.fds_owned;
    sibling.fds = &sibling.fds_owned;
    nas_fd_system_init();
    /* Linux open/pipe/socket/accept allocate the smallest free fd,
     * including stdin/stdout/stderr after close. These are only initially
     * reserved by convention, not by the FD allocator. */
    unsigned chosen[2], slots[2];
    assert(nas_fd_reserve(&p,0,2,chosen,slots,"paired-low-fd")==0);
    assert(chosen[0]==0 && chosen[1]==1 && slots[0]!=slots[1]);
    assert(nas_fd_install_new(&p,0,slots[0],FD_NULL,0,false));
    assert(nas_fd_install_new(&p,1,slots[1],FD_NULL,0,false));
    assert(nas_fd_reserve(&p,0,2,chosen,slots,"paired-low-fd")==0);
    assert(chosen[0]==2 && chosen[1]==3);
    assert(nas_fd_install_new(&p,2,slots[0],FD_NULL,0,false));
    assert(nas_fd_first_unused(&p,0)==3);
    assert(nas_fd_close(&p,0)<NAS_OPEN_FILE_LIMIT);
    assert(nas_fd_reserve(&p,0,2,chosen,slots,"paired-low-fd")==0);
    assert(chosen[0]==0 && chosen[1]==3);
    assert(nas_fd_close(&p,1)<NAS_OPEN_FILE_LIMIT);
    assert(nas_fd_close(&p,2)<NAS_OPEN_FILE_LIMIT);
    assert(nas_fd_first_unused(&p,0)==0);
    p.fds->entries[3] = (struct nas_fd_entry){FD_TMP, 120, 1};
    nas_open_files[120].refcount = 1;
    nas_open_files[120].position = 43;
    nas_open_files[120].status_flags = 2;

    assert(nas_fd_clone(&p, 3, 4, false) == 4);
    assert(nas_open_files[120].refcount == 2);
    assert(p.fds->entries[4].kind == FD_TMP);
    assert(p.fds->entries[4].slot == 120);
    assert(p.fds->entries[4].cloexec == 0);
    assert(p.fds->entries[3].cloexec == 1);
    assert(nas_fd_duplicate(&p, 3, 0, true) == 0);
    assert(p.fds->entries[0].cloexec == 1);
    assert(nas_open_files[120].refcount == 3);
    assert(nas_fd_clone(&p, 3, 4, false) == (uint64_t)-EBUSY);

    /* A fork copies the descriptor table and holds each alias once. */
    child.fds_owned = *p.fds;
    assert(nas_task_hold_files(&child));
    assert(nas_open_files[120].refcount == 6);
    assert(child.fds->entries[3].cloexec == 1);
    child.fds->entries[3].cloexec = 0;
    assert(p.fds->entries[3].cloexec == 1);
    assert(nas_open_files[120].position == 43);
    nas_open_files[120].position = 69;
    assert(nas_open_files[child.fds->entries[4].slot].position == 69);

    /* CLONE_FILES shares the TABLE, so it is not a fork hold. */
    sibling.fds = p.fds;
    sibling.fds->entries[4].cloexec = 1;
    assert(p.fds->entries[4].cloexec == 1);
    assert(nas_open_files[120].refcount == 6);

    assert(nas_fd_close(&child, 0) == NAS_OPEN_FILE_LIMIT);
    assert(nas_fd_close(&child, 3) == NAS_OPEN_FILE_LIMIT);
    assert(nas_fd_close(&child, 4) == NAS_OPEN_FILE_LIMIT);
    assert(nas_fd_close(&p, 0) == NAS_OPEN_FILE_LIMIT);
    assert(nas_fd_close(&p, 3) == NAS_OPEN_FILE_LIMIT);
    assert(nas_fd_close(&p, 4) == 120);
    assert(nas_fd_close(&p, 4) == NAS_OPEN_FILE_LIMIT);
    assert(nas_open_files[120].refcount == 0);
    assert(p.fds->entries[4].kind == FD_CLOSED);
    assert(p.fds->entries[4].slot == 0);
    assert(p.fds->entries[4].cloexec == 0);

    /* Reject stale or invalid inherited slots atomically. */
    p.fds->entries[0] = (struct nas_fd_entry){FD_PIPE_R, 200, 1};
    assert(!nas_task_hold_files(&p));
    assert(nas_open_files[120].refcount == 0);
    assert(nas_fd_duplicate(&p, 0, 1, false) == (uint64_t)-EBADF);
    puts("[M62 FD PASS] AoS, fork/CLONE_FILES, dup, CLOEXEC, last-close");
    return 0;
}
