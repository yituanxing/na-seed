/* Real fd.c OpenFile lease ownership: asynchronous accept may not expose a
 * slot that another Task can claim while native networking is outstanding. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/task.h"
#include "../kernel/fd.h"
#include "../kernel/errno.h"
#include "../kernel/fs/file_ops.h"

void nas_print(const char *s){(void)s;}
void nas_dec(uint64_t n){(void)n;}
__attribute__((noreturn)) void nas_arch_halt(void){
    assert(0 && "FD lease owner invariant");__builtin_trap();
}
const struct nas_file_ops *nas_fs_ops_for_kind(uint8_t kind){
    (void)kind;return NULL;
}
static const struct nas_file_ops stream_ops={.aggregate_readv=true};
static struct nas_task first,other;

int main(void){
    nas_fd_system_init();
    first.files=nas_files_new();other.files=nas_files_new();
    assert(first.files && other.files && first.files!=other.files);
    first.fds=&first.files->table;
    other.fds=&other.files->table;

    struct nas_fd_slot_lease lease={0},another={0};
    assert(nas_fd_slot_lease_begin(&lease,"pending accept")==0);
    assert(lease.slot==0 && lease.token);
    assert(nas_fd_slot_lease_valid(lease));
    assert(nas_open_files[lease.slot].refcount==1);
    assert(nas_open_files[lease.slot].lease_token==lease.token);
    unsigned fds[2],slots[2];
    /* Both Tasks see a fully owned OpenFile slot, although neither has an
     * FD for it. The old read-only reserve can no longer race this slot. */
    assert(nas_fd_reserve(&other,0,1,fds,slots,"competing Task")==0);
    assert(slots[0]!=lease.slot);
    assert(!nas_fd_install_new(&other,0,lease.slot,FD_SOCKET,2,false));
    assert(!nas_fd_attach_owned(&other,0,lease.slot,FD_SOCKET,false));
    assert(!nas_fd_hold_description(lease.slot));
    assert(nas_fd_slot_lease_begin(&another,"second pending accept")==0);
    assert(another.slot!=lease.slot && another.token!=lease.token);

    /* A concurrent Task can occupy the intended integer FD, but cannot
     * steal the reserved OpenFile. Failed publication preserves the lease. */
    assert(nas_fd_install_new(&other,0,slots[0],FD_NULL,0,false)==false);
    /* slots[0] is another active lease now; use the next free slot. */
    assert(nas_fd_reserve(&other,0,1,fds,slots,"other ordinary file")==0);
    assert(nas_fd_install_new(&other,0,slots[0],FD_NULL,0,false));
    assert(nas_fd_slot_lease_commit(lease,&other,0,FD_SOCKET,2,false,
                                     &stream_ops)==-EBUSY);
    assert(nas_fd_slot_lease_valid(lease));
    assert(!nas_fd_slot_lease_cancel((struct nas_fd_slot_lease){
        .slot=lease.slot,.token=another.token}));
    assert(nas_fd_slot_lease_commit(lease,&first,0,FD_SOCKET,0x802U,true,
                                     &stream_ops)==0);
    assert(!nas_fd_slot_lease_valid(lease));
    assert(!nas_fd_slot_lease_cancel(lease));
    assert(first.fds->entries[0].slot==lease.slot);
    assert(first.fds->entries[0].cloexec==1);
    assert(nas_open_files[lease.slot].status_flags==0x802U);
    assert(nas_open_files[lease.slot].ops==&stream_ops);
    assert(nas_open_files[lease.slot].lease_token==0);
    assert(nas_fd_clone(&first,0,1,false)==1);
    assert(nas_open_files[lease.slot].refcount==2);
    assert(nas_fd_close(&first,0)==NAS_OPEN_FILE_LIMIT);
    assert(nas_fd_close(&first,1)==lease.slot);
    nas_open_files[lease.slot]=(struct nas_open_file){0};

    /* Cancellation releases a kernel-only claim; a later incarnation has
     * a distinct token, so stale worker completions cannot publish it. */
    assert(nas_fd_slot_lease_cancel(another));
    struct nas_fd_slot_lease reused={0};
    assert(nas_fd_slot_lease_begin(&reused,"reused accept")==0);
    assert(reused.slot==lease.slot);
    assert(reused.token!=lease.token && reused.token!=another.token);
    assert(!nas_fd_slot_lease_cancel(lease));
    assert(nas_fd_slot_lease_cancel(reused));
    assert(nas_fd_close(&other,0)==slots[0]);
    nas_open_files[slots[0]]=(struct nas_open_file){0};

    struct nas_fd_slot_lease all[NAS_OPEN_FILE_LIMIT];
    for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++){
        assert(nas_fd_slot_lease_begin(&all[i],NULL)==0);
        assert(nas_fd_slot_lease_valid(all[i]));
    }
    struct nas_fd_slot_lease overflow={0};
    assert(nas_fd_slot_lease_begin(&overflow,NULL)==-ENFILE);
    assert(overflow.slot==NAS_OPEN_FILE_LIMIT && overflow.token==0);
    for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++)
        assert(nas_fd_slot_lease_cancel(all[i]));
    assert(!nas_fd_slot_lease_valid(all[0]));
    /* Warm reinit must not reuse a live old numeric generation token. */
    nas_fd_system_init();
    assert(nas_fd_slot_lease_begin(&reused,"warm reinit")==0);
    assert(reused.token!=all[0].token);
    assert(nas_fd_slot_lease_cancel(reused));
    puts("[FD SLOT LEASE PASS] OpenFile reservation, competing Task, atomic publish, dup, cancellation, stale generations and capacity");
}
