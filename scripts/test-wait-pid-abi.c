#include <assert.h>
#include <stdint.h>
#include "../kernel/task/wait_pid.h"
int main(void) {
    assert(nas_wait_pid_arg(UINT64_MAX)==-1);
    assert(nas_wait_pid_arg(UINT32_MAX)==-1);
    assert(nas_wait_pid_supported(UINT64_MAX));
    assert(nas_wait_pid_supported(UINT32_MAX));
    assert(nas_wait_pid_matches(UINT64_MAX,42));
    assert(nas_wait_pid_matches(UINT32_MAX,42));
    assert(nas_wait_pid_matches(42,42));
    assert(!nas_wait_pid_matches(42,43));
    assert(!nas_wait_pid_matches(0,42));
    assert(!nas_wait_pid_supported(0));
    assert(!nas_wait_pid_supported(UINT64_MAX-1));
    assert(!nas_wait_pid_supported(UINT32_MAX-1));
    assert(nas_wait_pid_supported(2147483647));
    assert(!nas_wait_pid_supported(2147483648));
    assert(nas_wait_pid_matches(0x1234500000002aULL,42));
    return 0;
}
