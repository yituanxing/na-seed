#include <assert.h>
#include <stdint.h>
#include "../kernel/task/wait_pid.h"
#include "../kernel/task/process.h"
int main(void) {
    assert(nas_wait_pid_arg(UINT64_MAX)==-1);
    /* Original Linux strace wait4(-1, ..., __WALL) must not be EINVAL.
     * Unsupported options are still rejected: no fake traced-TID support. */
    assert((NAS_WAIT_ALL & NAS_WAIT_SUPPORTED) == NAS_WAIT_ALL);
    assert((NAS_WAIT_SUPPORTED & 0x10000000ULL) == 0);
    assert(((NAS_WAIT_ALL | NAS_WAIT_NOHANG) & ~NAS_WAIT_SUPPORTED) == 0);
    assert(nas_wait_pid_arg(UINT32_MAX)==-1);
    assert(nas_wait_pid_matches(UINT64_MAX,42,1,7));
    assert(nas_wait_pid_matches(UINT32_MAX,42,1,7));
    assert(nas_wait_pid_matches(42,42,1,7));
    assert(!nas_wait_pid_matches(42,43,1,7));
    assert(nas_wait_pid_matches(0,43,7,7));
    assert(!nas_wait_pid_matches(0,43,7,8));
    assert(nas_wait_pid_matches((uint64_t)(int64_t)-9,55,1,9));
    assert(nas_wait_pid_matches((uint32_t)-9,55,1,9));
    assert(!nas_wait_pid_matches((uint32_t)-9,55,1,10));
    assert(!nas_wait_pid_matches((uint32_t)INT32_MIN,55,1,9));
    assert(nas_wait_pid_matches(0x1234500000002aULL,42,1,7));
    return 0;
}
