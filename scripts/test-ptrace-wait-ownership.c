/* M74: parent cannot steal trace stop (or reaping) from a separate tracer. */
#include <assert.h>
#include "../kernel/task.h"
#include "../kernel/task/process.h"
int main(void) {
    struct nas_task child={0};
    child.pid=10;child.ppid=1;child.tgid=10;
    child.ptrace_tracer_pid=3;
    child.state=NAS_TASK_STOPPED;child.stop_report_pending=true;
    child.stop_signal=5;
    assert(!nas_process_waitable(&child,0,1));
    assert(!nas_process_waitable(&child,NAS_WAIT_UNTRACED,1));
    assert(nas_process_waitable(&child,NAS_WAIT_ALL,3));
    assert(nas_process_waitable(&child,0,3));
    assert(!nas_process_waitable(&child,0,4));
    child.state=NAS_TASK_ZOMBIE;child.stop_report_pending=false;
    assert(!nas_process_waitable(&child,0,1));
    assert(nas_process_waitable(&child,NAS_WAIT_ALL,3));
    child.ptrace_tracer_pid=0;
    assert(nas_process_waitable(&child,0,1));
    child.state=NAS_TASK_STOPPED;child.stop_report_pending=true;
    assert(!nas_process_waitable(&child,0,1));
    assert(nas_process_waitable(&child,NAS_WAIT_UNTRACED,1));
    return 0;
}
