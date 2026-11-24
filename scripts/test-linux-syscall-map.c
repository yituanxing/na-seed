/* Whole-map regression: fingerprints derived from the pre-consolidation
 * exact-head decoder for raw 0..1024. Internal enum values are stable here.
 * Host compilation simulates only the decoder's architecture selection;
 * it is not an ISA execution test. Native CI still builds each ISA. */
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#if defined(NAS_TEST_RV64)
# undef __x86_64__
# define __riscv 1
# define __riscv_xlen 64
#elif defined(NAS_TEST_ARM64)
# undef __x86_64__
# define __aarch64__ 1
#endif
#include "../kernel/arch/linux_syscall_abi.h"
int main(void) {
    uint64_t hash=UINT64_C(14695981039346656037);
    unsigned count=0;
    for(uint64_t raw=0;raw<=1024;raw++) {
        enum nas_syscall_op op=nas_linux_decode_syscall(raw);
        if(op!=NAS_SC_NONE)count++;
        hash=(hash^(uint64_t)op)*UINT64_C(1099511628211);
    }
    assert(nas_linux_decode_syscall(UINT64_MAX)==NAS_SC_NONE);
#if defined(NAS_TEST_RV64)
    assert(count==185 && hash==UINT64_C(0x29cd676fb0e8f758));
#elif defined(NAS_TEST_ARM64)
    assert(count==184 && hash==UINT64_C(0xb9f723b1165c117d));
#else
    assert(count==212 && hash==UINT64_C(0xb3bfcdcbbeb45955));
#endif
    puts("[SYSCALL MAP HOST PASS] all raw IDs unchanged");
    return 0;
}
