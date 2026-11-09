/* Exercise production dispatch.c's common sigtimedwait completion helper.
 * Direct and scheduler-resumed paths must record PID1 shutdown intent only
 * AFTER the signal was genuinely consumed, not when kill() merely queued it.
 * The rest of dispatch.c is discarded by --gc-sections: this is deliberately
 * a fast HOST contract, NOT a native original BusyBox poweroff proof. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../kernel/syscall/dispatch.c"

static uint64_t next_signo;
static unsigned note_count,noted_signo;
static uint64_t last_mask,last_info;

uint64_t nas_signal_timedwait_consume(uint64_t mask,uint64_t info) {
    last_mask=mask;
    last_info=info;
    return next_signo;
}
void nas_init_note_pid1_shutdown_consumed(unsigned signal) {
    note_count++;
    noted_signo=signal;
}

int main(void) {
    struct nas_task pid1={.pid=1,.tgid=1};
    struct nas_task shell={.pid=2,.tgid=2};
    next_signo=NAS_SIGUSR2;
    assert(consume_sigtimedwait(&pid1,0x501U,0x6000U)==NAS_SIGUSR2);
    assert(note_count==1 && noted_signo==NAS_SIGUSR2);
    assert(last_mask==0x501U && last_info==0x6000U);

    /* The sleeping PID1 path completes via the SAME helper. */
    pid1.wait.blocked_args[0]=0x602U;
    pid1.wait.blocked_args[1]=0x7000U;
    next_signo=NAS_SIGTERM;
    assert(finish_sigtimedwait(&pid1)==NAS_SIGTERM);
    assert(note_count==2 && noted_signo==NAS_SIGTERM);
    assert(last_mask==0x602U && last_info==0x7000U);

    next_signo=NAS_SIGUSR1;
    assert(consume_sigtimedwait(&shell,0,0)==NAS_SIGUSR1);
    assert(note_count==2); /* shell's own signal is not PID1 intent */
    next_signo=(uint64_t)-EFAULT;
    assert(consume_sigtimedwait(&pid1,0,0)==(uint64_t)-EFAULT);
    assert(note_count==2); /* failed siginfo copy cannot count */
    next_signo=(uint64_t)-EAGAIN;
    assert(finish_sigtimedwait(&pid1)==(uint64_t)-EAGAIN);
    assert(note_count==2); /* timeout cannot count */
    next_signo=NAS_SIGCHLD;
    assert(consume_sigtimedwait(&pid1,0,0)==NAS_SIGCHLD);
    assert(note_count==2); /* child exit is not shutdown authorization */
    puts("M241_ORIGINAL_BUSYBOX_PID1_SIGWAIT_DISPATCH_PASS");
    return 0;
}
