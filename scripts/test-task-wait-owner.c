/* Test REAL task/wait.c lifetime reset without faking CPU/I/O providers.
 * Unused readiness code is link-garbage-collected in this host test only.
 * Full QEMU runs exercise real provider symbols on all 3 ISAs. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/task.h"
#include "../kernel/task/wait.h"
#include "../kernel/syscall/ops.h"

int main(void) {
    struct nas_task parent={0}, child={0};
    parent.pid=42;
    parent.tgid=42;
    struct nas_mm mm={.root_phys=0x1234000ULL};
    parent.mm=&mm;
    parent.wait.blocked_op=NAS_SC_ppoll;
    parent.wait.blocked_args[0]=7;
    parent.wait.pending_valid=true;
    parent.wait.deadline_ns=900;
    parent.wait.deadline_active=true;
    parent.wait.futex_addr=0xAABBCCDDULL;
    parent.wait.futex_mask=0x40U;
    parent.wait.futex_woken=true;
    parent.wait.tty_observed_bytes=9;
    nas_slot_mask_set(&parent.wait.poll_interest_read,3);
    nas_slot_mask_set(&parent.wait.poll_interest_write,126);
    parent.wait.poll_tty_read=true;
    assert(nas_slot_mask_any(&parent.wait.poll_interest_write));
    assert(nas_slot_mask_test(&parent.wait.poll_interest_write,126));
    /* Fork clones persistent resource state, but never parked waits. */
    child=parent;
    child.mm=0; /* Real nas_task_fork_slot clears inherited MM pointer. */
    child.pid=43;
    nas_task_wait_reset(&child);
    assert(child.pid==43 && child.tgid==42);
    assert(child.mm==0 && parent.mm->root_phys==0x1234000ULL);
    assert(!child.wait.pending_valid && !child.wait.blocked_op);
    assert(!child.wait.blocked_args[0] && !child.wait.deadline_active);
    assert(!child.wait.deadline_ns && !child.wait.futex_addr);
    assert(!child.wait.futex_mask && !child.wait.futex_woken);
    assert(!child.wait.tty_observed_bytes && !child.wait.poll_tty_read);
    assert(!nas_slot_mask_any(&child.wait.poll_interest_read));
    assert(!nas_slot_mask_any(&child.wait.poll_interest_write));
    nas_wait_io_arm(&child,126,4,false);
    assert(nas_wait_io_has_interest(&child));
    nas_wait_io_clear(&child);
    assert(!nas_wait_io_has_interest(&child));
    assert(parent.wait.pending_valid && parent.wait.deadline_active);
    assert(nas_slot_mask_test(&parent.wait.poll_interest_read,3));
    assert(nas_slot_mask_test(&parent.wait.poll_interest_write,126));
    /* Blocked syscall 0 is valid when pending=true, not "no syscall". */
    parent.wait.blocked_op=0;
    assert(parent.wait.pending_valid);
    nas_task_wait_reset(&parent);
    assert(!parent.wait.pending_valid && !parent.wait.poll_tty_read);
    nas_task_wait_reset(0);
    puts("[M64 WAIT PASS] per-thread ownership, fork reset, 128-slot interests");
    return 0;
}
