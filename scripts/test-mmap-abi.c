#include <assert.h>
#include <stdint.h>
#include "../kernel/mm/mmap_abi.h"
int main(void) {
    assert(nas_linux_anon_map_supported(0x22,UINT64_MAX,0));
    assert(nas_linux_anon_map_supported(0x22,UINT32_MAX,0));
    assert(nas_linux_anon_map_supported(0x22,0,0));
    assert(nas_linux_anon_map_supported(0x22,123,0));
    assert(nas_linux_anon_map_supported(0x4022,UINT64_MAX,0));
    assert(nas_linux_anon_map_supported(0x20022,UINT64_MAX,0));
    assert(nas_linux_anon_map_supported(0x24022,UINT64_MAX,0));
    assert(!nas_linux_anon_map_supported(0x20,UINT64_MAX,0));
    assert(!nas_linux_anon_map_supported(0x02,UINT64_MAX,0));
    assert(!nas_linux_anon_map_supported(0x23,UINT64_MAX,0));
    assert(!nas_linux_anon_map_supported(0x22,UINT64_MAX,4096));
    assert(nas_linux_anon_fixed_supported(0x32,0));
    assert(nas_linux_anon_fixed_supported(0x4032,0));
    assert(nas_linux_anon_fixed_supported(0x20032,0));
    assert(!nas_linux_anon_fixed_supported(0x22,0));
    assert(!nas_linux_anon_fixed_supported(0x32,4096));
    assert(!nas_linux_anon_fixed_supported(0x33,0));
    assert(!nas_linux_anon_map_supported(0x1022,UINT64_MAX,0));
    assert(!nas_linux_anon_fixed_supported(0x1032,0));
    return 0;
}
