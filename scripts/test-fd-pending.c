/* M180: exercise the REAL fd.c pending-FD owner, not a duplicate allocator.
 * In particular, CLONE_FILES and fork must behave differently while a
 * filesystem worker owns an unpublished openat result. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/task.h"
#include "../kernel/fd.h"
#include "../kernel/errno.h"

void nas_print(const char *s) { (void)s; }
void nas_dec(uint64_t n) { (void)n; }
__attribute__((noreturn)) void nas_arch_halt(void) {
    assert(0 && "pending FD invariant");
    __builtin_trap();
}
const struct nas_file_ops *nas_fs_ops_for_kind(uint8_t kind) {
    (void)kind;
    return 0;
}

int main(void) {
    nas_fd_system_init();
    struct nas_task owner={0},sibling={0},fork_child={0};
    owner.files=nas_files_new();
    assert(owner.files);
    owner.fds=&owner.files->table;
    sibling.files=nas_files_get(owner.files);
    sibling.fds=&sibling.files->table;

    unsigned reserved=NAS_FD_LIMIT;
    struct nas_files *held=NULL;
    assert(nas_fd_pending_begin(&owner,&reserved,&held)==0);
    assert(reserved==0 && held==owner.files && held->refs==3);
    assert(nas_fd_pending_valid(held,reserved));
    assert(!nas_fd_available(&owner,0) && !nas_fd_available(&sibling,0));
    assert(nas_fd_first_unused(&sibling,0)==1);
    unsigned fds[2],slots[2];
    assert(nas_fd_reserve(&sibling,0,2,fds,slots,NULL)==0);
    assert(fds[0]==1 && fds[1]==2);
    assert(!nas_fd_install_new(&sibling,0,slots[0],FD_NULL,0,false));
    assert(nas_fd_install_new(&sibling,1,slots[0],FD_NULL,0,false));
    assert(nas_fd_clone(&sibling,1,0,false)==(uint64_t)-EBUSY);
    assert(nas_fd_duplicate(&sibling,1,0,false)==2);

    /* fork copies installed descriptions, not the unpublished reservation. */
    fork_child.files=nas_files_copy(owner.files);
    assert(fork_child.files && !fork_child.files->pending_fd_mask);
    fork_child.fds=&fork_child.files->table;
    assert(nas_task_hold_files(&fork_child));
    assert(nas_fd_first_unused(&fork_child,0)==0);
    assert(nas_fd_install_new(&fork_child,0,slots[1],FD_NULL,0,false));

    /* The second outstanding open cannot reuse the first one's FD. */
    unsigned second=NAS_FD_LIMIT;
    struct nas_files *second_hold=NULL;
    assert(nas_fd_pending_begin(&sibling,&second,&second_hold)==0);
    assert(second==3 && second_hold==held && held->refs==4);
    assert(nas_fd_pending_cancel(held,reserved));
    assert(!nas_fd_pending_valid(second_hold,reserved));
    assert(nas_fd_first_unused(&owner,0)==0);
    assert(nas_fd_pending_commit(second_hold,second));
    assert(!nas_fd_pending_cancel(second_hold,second));

    /* Shared File table can disappear only after its pending owner lets go. */
    unsigned surviving=NAS_FD_LIMIT;
    struct nas_files *surviving_hold=NULL;
    assert(nas_fd_pending_begin(&owner,&surviving,&surviving_hold)==0);
    assert(surviving==0);
    assert(nas_fd_close(&owner,1)==NAS_OPEN_FILE_LIMIT);
    assert(nas_fd_close(&owner,2)==NAS_OPEN_FILE_LIMIT);
    nas_files_put(owner.files);
    nas_files_put(sibling.files);
    assert(surviving_hold->refs==1 &&
           nas_fd_pending_valid(surviving_hold,0));
    assert(nas_fd_pending_cancel(surviving_hold,0));

    assert(nas_fd_close(&fork_child,0)==slots[1]);
    assert(nas_fd_close(&fork_child,1)==NAS_OPEN_FILE_LIMIT);
    assert(nas_fd_close(&fork_child,2)==slots[0]);
    nas_open_files[slots[0]]=(struct nas_open_file){0};
    nas_open_files[slots[1]]=(struct nas_open_file){0};
    nas_files_put(fork_child.files);

    /* A global OpenFile lease alone must not bypass the pending FD bit.
     * The resumed Task publishes both claims on one nonpreemptible frame. */
    struct nas_task resumed={0};
    resumed.files=nas_files_new();
    assert(resumed.files);
    resumed.fds=&resumed.files->table;
    struct nas_files *open_hold=NULL;
    unsigned open_fd=NAS_FD_LIMIT;
    assert(nas_fd_pending_begin(&resumed,&open_fd,&open_hold)==0);
    struct nas_fd_slot_lease slot_lease={0};
    assert(nas_fd_slot_lease_begin(&slot_lease,"pending broker open")==0);
    assert(nas_fd_slot_lease_commit(slot_lease,&resumed,open_fd,
                                    FD_ARCHIVE,2,false,NULL)==-EBUSY);
    assert(nas_fd_slot_lease_valid(slot_lease));
    assert(nas_fd_pending_commit(open_hold,open_fd));
    assert(nas_fd_slot_lease_commit(slot_lease,&resumed,open_fd,
                                    FD_ARCHIVE,2,false,NULL)==0);
    assert(resumed.fds->entries[open_fd].slot==slot_lease.slot);
    assert(nas_fd_close(&resumed,open_fd)==slot_lease.slot);
    nas_open_files[slot_lease.slot]=(struct nas_open_file){0};
    nas_files_put(resumed.files);
    puts("[M180 FD PENDING PASS] CLONE_FILES/fork, dup exclusion, two pending opens, exit ref drain");
    return 0;
}
