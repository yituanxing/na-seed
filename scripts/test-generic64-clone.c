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
    assert(nas_linux_clone_child_tid(call)==0x1a1d30ULL);
    call[4]=UINT64_MAX; /* ignored TLS without CLONE_SETTLS */
    assert(nas_linux_clone_child_tid(call)==0x1a1d30ULL);
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
    /* Original AArch64 Go clone ignores the supplied arg4 without
     * CLONE_SETTLS; x86 Go variant explicitly supplies thread-local TLS. */
    call[1]=0x00050f00ULL;
    call[2]=0x3401e000ULL;
    call[3]=0x34002540ULL; /* ignored: no PARENT_SETTID flag */
    call[4]=0x00050f00ULL; /* ignored: no SETTLS */
    assert(nas_linux_decode_call(call)==NAS_SC_clone_thread);
    call[1]=0x000d0f00ULL;
    assert(nas_linux_decode_call(call)==NAS_SC_clone_thread);
    call[1]|=0x00200000ULL; /* unimplemented extra combination */
    assert(nas_linux_decode_call(call)==NAS_SC_NONE);
    /* Real RV64/AArch64 BusyBox Kconfig make invokes native /bin/sleep.
     * Linux asm-generic syscall 101 was already supported by the common
     * deadline scheduler, but missing from this architecture decoder. */
    assert(nas_linux_decode_syscall(101U)==NAS_SC_nanosleep);
    assert(nas_linux_decode_syscall(115U)==NAS_SC_clock_nanosleep);
    assert(nas_linux_decode_syscall(166U)==NAS_SC_umask);
    assert(nas_linux_decode_syscall(165U)==NAS_SC_getrusage);
    assert(nas_linux_decode_syscall(233U)==NAS_SC_madvise);
    assert(nas_linux_decode_syscall(227U)==NAS_SC_msync);
    assert(nas_linux_decode_syscall(54U)==NAS_SC_fchownat);
    assert(nas_linux_decode_syscall(55U)==NAS_SC_fchown);
    assert(nas_linux_decode_syscall(123U)==NAS_SC_sched_getaffinity);
    assert(nas_linux_decode_syscall(122U)==NAS_SC_sched_setaffinity);
    assert(nas_linux_decode_syscall(67U)==NAS_SC_pread64);
    assert(nas_linux_decode_syscall(68U)==NAS_SC_pwrite64);
    assert(nas_linux_decode_syscall(82U)==NAS_SC_fsync);
    assert(nas_linux_decode_syscall(83U)==NAS_SC_fdatasync);
    assert(nas_linux_decode_syscall(46U)==NAS_SC_ftruncate);
    assert(nas_linux_decode_syscall(45U)==NAS_SC_truncate);
    call[0]=93U;
    assert(nas_linux_decode_call(call)==NAS_SC_exit);
    return 0;
}
