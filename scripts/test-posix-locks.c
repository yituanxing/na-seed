#include <assert.h>
#include "../kernel/fs/posix_locks.h"
int main(void) {
    struct nas_posix_locks ls={0};
    assert(nas_locks_apply(&ls,100,NAS_LOCK_READ,10,20)==0);
    assert(nas_locks_conflict(&ls,200,NAS_LOCK_READ,12,15)==-1);
    assert(nas_locks_conflict(&ls,200,NAS_LOCK_WRITE,12,15)>=0);
    assert(nas_locks_apply(&ls,200,NAS_LOCK_WRITE,12,15)==1);
    assert(nas_locks_apply(&ls,100,NAS_LOCK_WRITE,13,17)==0);
    assert(nas_locks_conflict(&ls,200,NAS_LOCK_READ,14,16)>=0);
    assert(nas_locks_apply(&ls,100,NAS_LOCK_UNLOCK,14,16)==0);
    assert(nas_locks_conflict(&ls,200,NAS_LOCK_READ,14,16)==-1);
    assert(nas_locks_conflict(&ls,200,NAS_LOCK_READ,13,14)>=0);
    assert(nas_locks_apply(&ls,200,NAS_LOCK_READ,14,16)==0);
    nas_locks_release(&ls,100);
    assert(nas_locks_conflict(&ls,300,NAS_LOCK_WRITE,10,20)==-1);
    assert(nas_locks_conflict(&ls,300,NAS_LOCK_WRITE,14,16)>=0);
    nas_locks_release(&ls,200);
    assert(ls.count==0);
    assert(nas_locks_apply(&ls,1,NAS_LOCK_READ,0,UINT64_MAX)==0);
    assert(nas_locks_conflict(&ls,2,NAS_LOCK_WRITE,INT64_MAX,UINT64_MAX)>=0);
    return 0;
}
