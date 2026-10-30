#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include "../kernel/arch/linux_stat_abi.h"
#include "../kernel/arch/linux_syscall_abi.h"
#include "../kernel/task/rusage.h"

int main(void) {
    assert(nas_linux_decode_syscall(__NR_statfs)==NAS_SC_statfs);
    assert(nas_linux_decode_syscall(__NR_fstatfs)==NAS_SC_fstatfs);
    assert(sizeof(struct nas_linux_statfs)==120);
    assert(offsetof(struct nas_linux_statfs,flags)==80);
    assert(sizeof(struct nas_linux_rusage)==144);
    struct nas_linux_rusage usage=nas_rusage_from_cpu_ns(2423456789ULL);
    assert(usage.utime.sec==2 && usage.utime.usec==423456);
    assert(usage.stime.sec==0 && usage.maxrss==0 && usage.nivcsw==0);
    usage=nas_rusage_from_cpu_ns(900ULL);
    assert(usage.utime.sec==0 && usage.utime.usec==0);
    return 0;
}
