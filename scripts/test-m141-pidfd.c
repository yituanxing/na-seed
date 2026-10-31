/* Host exercise of the actual pidfd provider and shared File allocator.
 * Task identities are controlled fixtures; no program-name shortcuts or
 * host Linux pidfd calls are used to generate expected results. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/errno.h"
#include "../kernel/task.h"
#include "../kernel/fd.h"
#include "../kernel/fs/file_ops.h"
#include "../kernel/fs/pidfd.h"

static struct nas_task owner,target,thread_peer;
struct nas_task *nas_current_task=&owner;
void nas_print(const char *s) {(void)s;}
void nas_dec(uint64_t n) {(void)n;}
__attribute__((noreturn)) void nas_arch_halt(void) {
    assert(0 && "host FD owner panic");
    __builtin_trap();
}
struct nas_task *nas_task_by_pid(uint64_t pid) {
    struct nas_task *tasks[]={&owner,&target,&thread_peer};
    for(unsigned i=0;i<sizeof(tasks)/sizeof(tasks[0]);i++)
        if(tasks[i]->state!=NAS_TASK_FREE && tasks[i]->pid==pid)
            return tasks[i];
    return NULL;
}
struct nas_task *nas_task_at(unsigned slot) {
    struct nas_task *tasks[]={&owner,&target,&thread_peer};
    return slot<sizeof(tasks)/sizeof(tasks[0])?tasks[slot]:NULL;
}
static const struct nas_file_ops pidfd_ops={
    .read=nas_pidfd_read,
    .poll=nas_pidfd_poll_fd,
    .poll_description=nas_pidfd_poll_description,
    .release=nas_pidfd_release
};
const struct nas_file_ops *nas_fs_ops_for_kind(uint8_t kind) {
    return kind==FD_PIDFD?&pidfd_ops:NULL;
}
static void final_close(struct nas_task *task,unsigned fd,unsigned slot) {
    assert(nas_fd_close(task,fd)==slot);
    assert(nas_open_files[slot].ops && nas_open_files[slot].ops->release);
    nas_open_files[slot].ops->release(slot);
    nas_open_files[slot]=(struct nas_open_file){0};
}

int main(void) {
    nas_fd_system_init();
    owner.pid=1;owner.tgid=1;owner.state=NAS_TASK_RUNNING;
    owner.files=nas_files_new();
    assert(owner.files);
    owner.fds=&owner.files->table;
    target.pid=42;target.tgid=42;target.state=NAS_TASK_READY;
    thread_peer.pid=43;thread_peer.tgid=42;
    thread_peer.state=NAS_TASK_READY;

    /* No allocations on invalid request, wrong flags or missing target. */
    assert(nas_pidfd_open(0,0)==(uint64_t)-EINVAL);
    assert(nas_pidfd_open((uint64_t)-1,0)==(uint64_t)-EINVAL);
    assert(nas_pidfd_open(42,1)==(uint64_t)-EINVAL);
    assert(nas_pidfd_open(99,0)==(uint64_t)-ESRCH);
    assert(!nas_open_files[0].refcount);

    uint64_t result=nas_pidfd_open(42,0);
    assert(result==0);
    unsigned slot=owner.fds->entries[result].slot;
    assert(owner.fds->entries[result].kind==FD_PIDFD);
    assert(owner.fds->entries[result].cloexec==1);
    assert(nas_open_files[slot].refcount==1);
    assert(nas_open_files[slot].ops==&pidfd_ops);
    assert(nas_open_files[slot].status_flags==0);
    assert(nas_pidfd_poll_fd(result,1)==0);
    assert(nas_pidfd_poll_description(slot,1)==0);
    assert(nas_pidfd_read(slot,NULL,1)==(uint64_t)-EINVAL);

    /* dup and fork do not create a second task lookup or copy a pidfd
     * backend identity; CLOEXEC is descriptor-local. */
    assert(nas_fd_clone(&owner,result,1,false)==1);
    assert(owner.fds->entries[1].cloexec==0);
    struct nas_task child={0};
    child.files=nas_files_copy(owner.files);
    assert(child.files);
    child.fds=&child.files->table;
    assert(nas_task_hold_files(&child));
    assert(nas_open_files[slot].refcount==4);
    assert(nas_fd_close(&owner,0)==NAS_OPEN_FILE_LIMIT);
    assert(nas_fd_close(&owner,1)==NAS_OPEN_FILE_LIMIT);
    assert(nas_fd_close(&child,0)==NAS_OPEN_FILE_LIMIT);
    assert(nas_open_files[slot].refcount==1);

    /* Linux's default pidfd watches the whole thread group; exiting its
     * leader alone must not wake an EPOLLIN waiter. */
    target.state=NAS_TASK_ZOMBIE;
    assert(nas_pidfd_poll_description(slot,1)==0);
    thread_peer.state=NAS_TASK_ZOMBIE;
    assert(nas_pidfd_poll_description(slot,1)==0x41);
    target.state=NAS_TASK_FREE; /* wait4 reaped; old PID is never recycled */
    assert(nas_pidfd_poll_description(slot,1)==0x51);
    assert(nas_pidfd_poll_description(slot,0)==0x10);
    final_close(&child,1,slot);
    nas_files_put(child.files);
    nas_files_put(owner.files);
    assert(nas_pidfd_poll_description(slot,1)==0);

    /* Capacity failures may not strand a half-installed description. */
    owner.files=nas_files_new();
    assert(owner.files);
    owner.fds=&owner.files->table;
    target.state=NAS_TASK_READY;
    for(unsigned i=0;i<NAS_FD_LIMIT;i++)
        owner.fds->entries[i]=(struct nas_fd_entry){.kind=FD_NULL};
    assert(nas_pidfd_open(42,0)==(uint64_t)-EMFILE);
    for(unsigned i=0;i<NAS_FD_LIMIT;i++)
        owner.fds->entries[i]=(struct nas_fd_entry){0};
    for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++)
        nas_open_files[i].refcount=1;
    assert(nas_pidfd_open(42,0)==(uint64_t)-ENFILE);
    for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++)
        nas_open_files[i]=(struct nas_open_file){0};
    result=nas_pidfd_open(42,0);
    assert(result==0);
    slot=owner.fds->entries[result].slot;
    assert(nas_pidfd_poll_description(slot,1)==0);
    final_close(&owner,0,slot);
    nas_files_put(owner.files);

    puts("[M141 PIDFD HOST PASS] ABI errors, capacity rollback, CLOEXEC, fork/dup File identity, leader/thread exit, reap HUP, final-close reuse");
    return 0;
}
