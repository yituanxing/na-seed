/* Exercise the REAL shared signal-user-return code with an injected
 * architecture sigframe construction fault. Never reboot/kill the kernel:
 * retire only the faulty userspace Task and resume an unaffected Task with
 * an authentic saved-register return value. */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "../kernel/task.h"
#include "../kernel/task/signal.h"
#include "../kernel/arch/arch.h"
static struct nas_task bad,good;
static struct nas_sighand bad_sighand,good_sighand;
struct nas_task *nas_current_task;
static unsigned failures,group_kills,system_panics;
static uint64_t delivered,returned;
static bool reject_bad;
void nas_arch_halt(void) {++system_panics;assert(0 && "kernel panic forbidden");}
void nas_print(const char *s){(void)s;}
bool nas_arch_signal_install_handler(uint64_t *frame,unsigned sig,
                                     const uint64_t action[4]) {
    (void)frame;(void)action;
    ++failures;
    if(nas_current_task==&bad && reject_bad)return false;
    delivered=sig;
    return true;
}
void nas_sched_thread_group_exit(uint32_t code,uint32_t sig) {
    assert(code==0 && sig==NAS_SIGSEGV);
    assert(nas_current_task==&bad);
    ++group_kills;
    bad.state=NAS_TASK_ZOMBIE;
}
uint64_t nas_task_schedule(uint64_t *frame) {
    assert(bad.state==NAS_TASK_ZOMBIE && nas_current_task==&bad);
    nas_current_task=&good;
    good.state=NAS_TASK_RUNNING;
    frame[0]=0; /* old syscall's rax must not leak to the next Task */
    return 77;
}
void nas_arch_set_saved_return(struct nas_task *t,uint64_t result) {
    assert(t==&good); returned=result;
}
void nas_arch_load_user_frame(const struct nas_task *t,uint64_t *frame) {
    assert(t==&good && returned==77);
    frame[0]=returned;
}
int main(void) {
    uint64_t frame[48]={0};
    bad=(struct nas_task){0};
    good=(struct nas_task){0};
    bad_sighand=(struct nas_sighand){0};
    good_sighand=(struct nas_sighand){0};
    bad.sighand=&bad_sighand;
    good.sighand=&good_sighand;
    bad.state=NAS_TASK_RUNNING;
    good.state=NAS_TASK_READY;
    bad.pending_signals=1ULL<<(NAS_SIGUSR1-1U);
    good.pending_signals=1ULL<<(NAS_SIGUSR2-1U);
    bad_sighand.actions[NAS_SIGUSR1][0]=0x1234;
    good_sighand.actions[NAS_SIGUSR2][0]=0x5678;
    nas_current_task=&bad;
    reject_bad=true;
    nas_signal_user_return(frame);
    assert(nas_current_task==&good && bad.state==NAS_TASK_ZOMBIE);
    assert(group_kills==1 && system_panics==0 && failures==2);
    assert(delivered==NAS_SIGUSR2 && frame[0]==77);
    assert(!(good.pending_signals&(1ULL<<(NAS_SIGUSR2-1U))));
    /* No Task with a pending handler: must not switch or touch any frame. */
    nas_signal_user_return(frame);
    assert(group_kills==1 && failures==2);
    nas_signal_user_return(0);
    assert(group_kills==1);
    puts("[M100 SIGFRAME PASS] failed user signal frame kills only its Task, native return resumes a second Task");
    return 0;
}
