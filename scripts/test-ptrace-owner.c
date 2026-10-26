#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../kernel/task.h"
#include "../kernel/task/ptrace.h"
#include "../kernel/syscall/ops.h"
#include "../kernel/errno.h"

static struct nas_task tracer, tracee, stranger;
static unsigned ptrace_notifications;
static uint64_t peek_result;
static uint8_t signal_result[128];
void nas_signal_ptrace_stop(const struct nas_task *tracee){
    if(tracee && tracee->ptrace_tracer_pid && tracee->stop_report_pending)
        ptrace_notifications++;
}
uint64_t nas_signal_kill(uint64_t pid,uint64_t signo,uint64_t *frame){
    (void)pid;(void)signo;(void)frame;return 0;
}
struct nas_task *nas_task_at(unsigned slot){
    return slot==0?&tracer:slot==1?&tracee:slot==2?&stranger:0;
}
uint64_t nas_arch_signal_user_sp(void){return 0;}
void nas_arch_save_user_frame(struct nas_task *t,const uint64_t *f){(void)t;(void)f;}
void nas_arch_save_fpu(struct nas_task *t){(void)t;}
int64_t nas_vm_read_task_bytes(const struct nas_task *t,uint64_t va,uint8_t *dst,uint64_t n){
    if(t!=&tracee || va!=0x1000 || n!=8)return -EFAULT;
    const uint64_t expected=UINT64_C(0xf0e1d2c3b4a59687);
    memcpy(dst,&expected,8);
    return 8;
}
int64_t nas_vm_write_task_bytes(const struct nas_task *t,uint64_t va,const uint8_t *src,uint64_t n){(void)t;(void)va;(void)src;(void)n;return -EFAULT;}
bool nas_user_read_bytes(uint64_t a,uint8_t *d,uint64_t n){(void)a;(void)d;(void)n;return false;}
bool nas_user_write_bytes(uint64_t a,const uint8_t *s,uint64_t n){
    if(a!=(uint64_t)(uintptr_t)signal_result || n!=sizeof(signal_result))return false;
    memcpy(signal_result,s,(size_t)n);
    return true;
}
bool nas_user_write_u64(uint64_t a,uint64_t v){
    if(a!=(uint64_t)(uintptr_t)&peek_result)return false;
    peek_result=v;
    return true;
}

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
    /* A legacy ptrace-only kernel must reject SEIZE with EIO, NOT
     * EPERM: upstream strace interprets EPERM as supporting SEIZE. */
    assert((int64_t)nas_ptrace_request(0x4206,3,0,0)==-EIO);
    assert((int64_t)nas_ptrace_request(0x4207,3,0,0)==-EIO);
    assert((int64_t)nas_ptrace_request(7,2,0,0)==-EPERM);
    nas_current_task=&tracer;
    assert((int64_t)nas_ptrace_request(7,2,0,0)==-ESRCH);
    tracee.state=NAS_TASK_STOPPED;
    tracee.stopped_from=NAS_TASK_READY;
    tracee.stop_report_pending=true;
    /* Linux ptrace options own only actually implemented event semantics. */
    assert(nas_ptrace_request(0x4200,2,0,1)==0);
    assert(tracee.ptrace_options==1);
    assert(nas_ptrace_request(0x4200,2,0,0x51)==0);
    assert(nas_ptrace_request(0x4200,2,0,0x5f)==0);
    assert(tracee.ptrace_options==0x5f);
    assert(nas_ptrace_request(0x4200,2,0,0x51)==0);
    assert(tracee.ptrace_options==0x51);
    assert((int64_t)nas_ptrace_request(0x4200,2,0,0x40000000ULL)==-EINVAL);
    assert(tracee.ptrace_options==0x51);
    /* Exercise the *raw kernel* Linux ptrace PEEK ABI (libc wrappers
     * translate this output pointer into a returned word). Reading real
     * tracee memory must not leak the word as a syscall result. */
    peek_result=0;
    assert(nas_ptrace_request(2,2,0x1000,(uint64_t)(uintptr_t)&peek_result)==0);
    assert(peek_result==UINT64_C(0xf0e1d2c3b4a59687));
    peek_result=0;
    assert(nas_ptrace_request(1,2,0x1000,(uint64_t)(uintptr_t)&peek_result)==0);
    assert(peek_result==UINT64_C(0xf0e1d2c3b4a59687));
    assert((int64_t)nas_ptrace_request(2,2,0x1000,0)==-EFAULT);
    assert((int64_t)nas_ptrace_request(2,2,0xdead,(uint64_t)(uintptr_t)&peek_result)==-EFAULT);
#if defined(__x86_64__)
    peek_result=0;
    assert(nas_ptrace_request(3,2,17*8,(uint64_t)(uintptr_t)&peek_result)==0);
    assert(peek_result==0x33); /* Real x86-64 userspace CS selector. */
    assert((int64_t)nas_ptrace_request(3,2,17*8,0)==-EFAULT);
    peek_result=UINT64_C(0x1234);
    assert(nas_ptrace_request(3,2,848,(uint64_t)(uintptr_t)&peek_result)==0);
    assert(peek_result==0); /* DR0 probe must initialize output, not merely return zero. */
#endif
    assert(nas_ptrace_request(24,2,0,0)==0);
    assert(tracee.state==NAS_TASK_READY && tracee.ptrace_trace_syscalls);
    tracee.state=NAS_TASK_STOPPED;
    tracee.stopped_from=NAS_TASK_READY;
    tracee.stop_report_pending=true;
    assert(nas_ptrace_request(7,2,0,0)==0);
    assert(tracee.state==NAS_TASK_READY && !tracee.stop_report_pending);
    tracee.state=NAS_TASK_STOPPED;
    tracee.stopped_from=NAS_TASK_BLOCKED;
    assert(nas_ptrace_request(17,2,0,0)==0);
    assert(tracee.state==NAS_TASK_BLOCKED && !tracee.ptrace_tracer_pid);
    /* A tracer attaches to a distinct READY sibling; real parent remains
     * unchanged and wait4 gets one real SIGSTOP report. */
    nas_current_task=&tracer;
    stranger.state=NAS_TASK_READY;
    stranger.ppid=19;
    stranger.tgid=3;
    stranger.resume_valid=true;
    tracer.tgid=1;
    assert(nas_ptrace_request(16,3,0,0)==0);
    assert(stranger.ppid==19 && stranger.ptrace_tracer_pid==1);
    assert(stranger.state==NAS_TASK_STOPPED &&
           stranger.stopped_from==NAS_TASK_READY &&
           stranger.stop_signal==19 && stranger.stop_report_pending);
    assert(ptrace_notifications==1);
    assert((int64_t)nas_ptrace_request(16,3,0,0)==-EPERM);
    assert(nas_ptrace_request(17,3,0,0)==0);
    assert(stranger.state==NAS_TASK_READY && !stranger.ptrace_tracer_pid);
    /* An existing blocked Task may be attached and resumed without
     * executing or discarding its original pending syscall. */
    stranger.state=NAS_TASK_BLOCKED;
    stranger.wait.pending_valid=true;
    stranger.wait.blocked_op=NAS_SC_nanosleep;
    assert(nas_ptrace_request(16,3,0,0)==0);
    /* Attach preserves the original blocked syscall's C continuation. */
    assert(stranger.wait.pending_valid && stranger.state==NAS_TASK_STOPPED);
    assert(nas_ptrace_request(17,3,0,0)==0);
    assert(stranger.state==NAS_TASK_BLOCKED && stranger.wait.pending_valid);
    /* Real CPU breakpoint must report through the same authenticated
     * STOP / SIGCHLD event path; a syscall event must not be invented. */
    assert(nas_ptrace_request(16,3,0,0)==0);
    assert(nas_ptrace_request(7,3,0,0)==0);
    stranger.state=NAS_TASK_RUNNING;
    stranger.resume_valid=false;
    assert(nas_ptrace_user_exception(&stranger,5,0x80,0x1021,0x3000,0x1020));
    assert(stranger.state==NAS_TASK_STOPPED && stranger.stop_signal==5);
    assert(stranger.ptrace_ip==0x1021 && stranger.ptrace_sp==0x3000);
    assert(stranger.ptrace_phase==0 && stranger.stop_report_pending);
    nas_current_task=&tracer;
    assert(nas_ptrace_request(0x4202,3,0,(uint64_t)(uintptr_t)signal_result)==0);
    int32_t code,signo;
    uint64_t address;
    memcpy(&signo,signal_result,4);
    memcpy(&code,signal_result+8,4);
    memcpy(&address,signal_result+16,8);
    assert(signo==5 && code==0x80 && address==0x1020);
    assert((int64_t)nas_ptrace_request(0x4202,3,0,0)==-EFAULT);
    return 0;
}
