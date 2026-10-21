#include <assert.h>
#include <stdint.h>
/* Host-executable emulator for *header-only* asm-generic Linux syscall
 * number dispatch. Suppress host x86 ISA macro after libc headers load. */
#undef __x86_64__
#define __riscv 1
#define __riscv_xlen 64
#include "../kernel/arch/linux_syscall_abi.h"
int main(void) {
    uint64_t call[8]={220,0x01200011ULL,0,0,0,0x1a1d30ULL,0,0};
    assert(nas_linux_decode_call(call)==NAS_SC_fork_child_tid);
    call[5]=0; /* CHILD_SETTID without pointer has no safe implementation. */
    assert(nas_linux_decode_call(call)==NAS_SC_NONE);
    call[5]=0x1a1d30ULL;
    call[2]=0x70000000ULL; /* alternate child stack is unsupported */
    assert(nas_linux_decode_call(call)==NAS_SC_NONE);
    call[2]=0;
    call[1]=0x1200011ULL|0x100ULL; /* CLONE_VM changes process semantics */
    assert(nas_linux_decode_call(call)==NAS_SC_NONE);
    call[1]=0x1200011ULL|0x4000ULL; /* CLONE_VFORK is not fork */
    assert(nas_linux_decode_call(call)==NAS_SC_NONE);
    call[1]=17U;
    call[5]=0;
    assert(nas_linux_decode_call(call)==NAS_SC_fork);
    call[0]=93U;
    assert(nas_linux_decode_call(call)==NAS_SC_exit);
    return 0;
}
