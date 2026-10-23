#include <assert.h>
#include "../kernel/task/cpu_affinity.h"
int main(void) {
    assert(NAS_CPU_AFFINITY_BYTES==8);
    assert(NAS_CPU_AFFINITY_MASK==1ULL);
    assert(nas_cpu_affinity_valid_size(8));
    assert(!nas_cpu_affinity_valid_size(7));
    assert(nas_cpu_affinity_valid_size(128));
    assert(nas_cpu_affinity_allowed(1));
    assert(nas_cpu_affinity_allowed(17));
    assert(!nas_cpu_affinity_allowed(2));
    assert(!nas_cpu_affinity_allowed(0));
    return 0;
}
