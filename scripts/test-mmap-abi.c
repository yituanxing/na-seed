#include <assert.h>
#include <stdint.h>
#include "../kernel/mm/mmap_abi.h"
int main(void) {
    assert(nas_linux_anon_map_supported(0x22,UINT64_MAX,0));
    assert(nas_linux_anon_map_supported(0x22,UINT32_MAX,0));
    assert(nas_linux_anon_map_supported(0x22,0,0));
    assert(nas_linux_anon_map_supported(0x22,123,0));
    assert(!nas_linux_anon_map_supported(0x20,UINT64_MAX,0));
    assert(!nas_linux_anon_map_supported(0x02,UINT64_MAX,0));
    assert(!nas_linux_anon_map_supported(0x23,UINT64_MAX,0));
    assert(!nas_linux_anon_map_supported(0x22,UINT64_MAX,4096));
    return 0;
}
