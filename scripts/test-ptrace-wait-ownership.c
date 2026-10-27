/* M74: parent cannot steal trace stop (or reaping) from a separate tracer. */
#include <assert.h>
#include "../kernel/task.h"
#include "../kernel/task/process.h"
int main(void) {
    struct nas_task child={0},parent={0},tracer={0},foreign={0},worker={0};
    parent.pid=1;parent.tgid=1;
    tracer.pid=3;tracer.tgid=3;
    foreign.pid=4;foreign.tgid=4;
    worker.pid=27;worker.tgid=1;worker.is_thread=true;
    child.pid=10;child.ppid=1;child.tgid=10;
    child.ptrace_tracer_pid=3;
    child.state=NAS_TASK_STOPPED;child.stop_report_pending=true;
    child.stop_signal=5;
    assert(!nas_process_waitable(&child,0,&parent));
    assert(!nas_process_waitable(&child,NAS_WAIT_UNTRACED,&parent));
    assert(nas_process_waitable(&child,NAS_WAIT_ALL,&tracer));
    assert(nas_process_waitable(&child,0,&tracer));
    assert(!nas_process_waitable(&child,0,&foreign));
    child.state=NAS_TASK_ZOMBIE;child.stop_report_pending=false;
    assert(!nas_process_waitable(&child,0,&parent));
    assert(nas_process_waitable(&child,NAS_WAIT_ALL,&tracer));
    child.ptrace_tracer_pid=0;
    assert(nas_process_waitable(&child,0,&parent));
    child.state=NAS_TASK_STOPPED;child.stop_report_pending=true;
    assert(!nas_process_waitable(&child,0,&parent));
    assert(nas_process_waitable(&child,NAS_WAIT_UNTRACED,&parent));
    child.state=NAS_TASK_ZOMBIE;
    assert(nas_process_wait_owner(&child,&worker));
    assert(nas_process_waitable(&child,0,&worker));
    child.ptrace_tracer_pid=3;
    assert(!nas_process_waitable(&child,0,&worker));
    assert(nas_process_waitable(&child,NAS_WAIT_ALL,&tracer));
    assert(!nas_process_wait_owner(&child,&foreign));
    return 0;
}
