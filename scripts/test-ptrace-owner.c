#include <assert.h>
#include <stdint.h>
#include "../kernel/task.h"
#include "../kernel/task/ptrace.h"
#include "../kernel/errno.h"

static struct nas_task tracer, tracee, stranger;
uint64_t nas_arch_signal_user_sp(void){return 0;}
void nas_arch_save_user_frame(struct nas_task *t,const uint64_t *f){(void)t;(void)f;}
void nas_arch_save_fpu(struct nas_task *t){(void)t;}
int64_t nas_vm_read_task_bytes(const struct nas_task *t,uint64_t va,uint8_t *dst,uint64_t n){(void)t;(void)va;(void)dst;(void)n;return -EFAULT;}
bool nas_user_read_bytes(uint64_t a,uint8_t *d,uint64_t n){(void)a;(void)d;(void)n;return false;}
bool nas_user_write_bytes(uint64_t a,const uint8_t *s,uint64_t n){(void)a;(void)s;(void)n;return false;}
bool nas_user_write_u64(uint64_t a,uint64_t v){(void)a;(void)v;return false;}

struct nas_task *nas_current_task;
struct nas_task *nas_task_by_pid(uint64_t pid) {
    if(pid==tracer.pid)return &tracer;
    if(pid==tracee.pid)return &tracee;
    if(pid==stranger.pid)return &stranger;
    return 0;
}
int main(void) {
    tracer.pid=1;tracer.state=NAS_TASK_READY;
    tracee.pid=2;tracee.ppid=1;tracee.state=NAS_TASK_RUNNING;
    stranger.pid=3;stranger.state=NAS_TASK_RUNNING;
    nas_current_task=&tracee;
    assert(nas_ptrace_request(0,0,0,0)==0);
    assert(tracee.ptrace_tracer_pid==tracer.pid);
    assert((int64_t)nas_ptrace_request(0,0,0,0)==-EPERM);
    nas_current_task=&stranger;
    assert((int64_t)nas_ptrace_request(7,2,0,0)==-EPERM);
    nas_current_task=&tracer;
    assert((int64_t)nas_ptrace_request(7,2,0,0)==-ESRCH);
    tracee.state=NAS_TASK_STOPPED;
    tracee.stopped_from=NAS_TASK_READY;
    tracee.stop_report_pending=true;
    assert((int64_t)nas_ptrace_request(24,2,0,0)==-EOPNOTSUPP);
    assert(tracee.state==NAS_TASK_STOPPED);
    assert(nas_ptrace_request(7,2,0,0)==0);
    assert(tracee.state==NAS_TASK_READY && !tracee.stop_report_pending);
    tracee.state=NAS_TASK_STOPPED;
    tracee.stopped_from=NAS_TASK_BLOCKED;
    assert(nas_ptrace_request(17,2,0,0)==0);
    assert(tracee.state==NAS_TASK_BLOCKED && !tracee.ptrace_tracer_pid);
    return 0;
}
