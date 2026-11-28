#ifndef NASEED_LINUX_SYSCALL_ABI_H
#define NASEED_LINUX_SYSCALL_ABI_H
#include <stdint.h>
#include "../syscall/ops.h"
#include "../errno.h"
#if defined(__x86_64__)
# include "x86_64/syscall.h"
#endif
/* Native-x86-only Linux operation; do not link an x86 syscall provider
 * into RV64 or ARM64 common dispatch. Unsupported returns -ENOSYS. */
static inline uint64_t nas_linux_arch_prctl(uint64_t op, uint64_t arg) {
#if defined(__x86_64__)
    return nas_arch_prctl(op, arg);
#else
    (void)op; (void)arg;
    return (uint64_t)(int64_t)-ENOSYS;
#endif
}
/* Decode real Linux register syscall numbers once at the arch/core boundary.
 * RISC-V and AArch64 use Linux asm-generic numbers; x86 has a separate table.
 * The separate argument decoder handles the supported fork, vfork and
 * thread-clone flag combinations; all other combinations return NONE.
 * Unsupported/legacy syscalls map to NONE and are reported as -ENOSYS. */
#if defined(__x86_64__)
# include "../nr.h" /* pinned native x86 table; only the decoder includes it */
#elif !(defined(__aarch64__) || (defined(__riscv) && __riscv_xlen == 64))
# error "Linux syscall ABI decoder required for this architecture"
#endif
static inline enum nas_syscall_op nas_linux_decode_syscall(uint64_t number) {
    switch (number) {
#if defined(__x86_64__)
# define NAS_LINUX_BOTH(op, nr) case __NR_##op: return NAS_SC_##op;
# define NAS_LINUX_X86(op) case __NR_##op: return NAS_SC_##op;
# define NAS_LINUX_RV64(op, nr)
#else
# define NAS_LINUX_BOTH(op, nr) case nr: return NAS_SC_##op;
# define NAS_LINUX_X86(op)
# if defined(__riscv) && __riscv_xlen == 64
#  define NAS_LINUX_RV64(op, nr) case nr: return NAS_SC_##op;
# else
#  define NAS_LINUX_RV64(op, nr)
# endif
#endif
#include "linux_syscall_map.def"
#undef NAS_LINUX_BOTH
#undef NAS_LINUX_X86
#undef NAS_LINUX_RV64
    default: return NAS_SC_NONE;
    }
}

/* The Linux syscall ABI supplies syscall-specific arguments, not just a
 * number. The only accepted legacy clone form is clone(SIGCHLD, 0, ...).
 * ptr arguments are ignored by Linux unless matching CLONE_* flags are
 * selected; do not reject unspecified garbage registers from libc.
 * This uses the EXISTING generic fork/task/VM algorithm, not a new clone
 * implementation. Never treat a thread clone as a process fork.
 */
/* Native Linux clone syscall registers differ between ISAs:
 * x86-64:      flags,stack,parent_tid,child_tid,tls
 * asm-generic: flags,stack,parent_tid,tls,child_tid
 * Common process code must never guess the ABI-specific register slot. */
static inline uint64_t nas_linux_clone_tls(const uint64_t *call) {
#if defined(__x86_64__)
    return call[5];
#else
    return call[4];
#endif
}
static inline uint64_t nas_linux_clone_child_tid(const uint64_t *call) {
#if defined(__x86_64__)
    return call[4];
#else
    return call[5];
#endif
}
static inline uint64_t nas_linux_raw_clone_nr(void) {
#if defined(__x86_64__)
    return __NR_clone;
#else
    return 220U; /* asm-generic Linux clone */
#endif
}
/* The x86 legacy vfork number has NO user stack argument; clone does.
 * Keep this register-ABI decision out of the common process dispatcher. */
static inline uint64_t nas_linux_vfork_child_stack(const uint64_t *call) {
#if defined(__x86_64__)
    if (call[0] == __NR_vfork) return 0;
#endif
    return call[2];
}
static inline enum nas_syscall_op nas_linux_decode_call(const uint64_t *args) {
    if (!args) return NAS_SC_NONE;
    if (args[0] == nas_linux_raw_clone_nr()) {
        /* musl posix_spawn uses CLONE_VM|CLONE_VFORK|SIGCHLD with
         * its own userspace child stack. Unsupported thread clones are ENOSYS. */
        /* Native glibc and musl pthreads request the SAME shared thread
         * semantics. Musl additionally passes CLONE_DETACHED (0x400000),
         * documented by Linux UAPI as unused/ignored. Accept precisely
         * those two forms without widening any required ownership flags. */
        const uint64_t pthread_flags=0x100ULL|0x200ULL|0x400ULL|0x800ULL|
            0x10000ULL|0x40000ULL|0x80000ULL|0x100000ULL|0x200000ULL;
        if ((args[1] & ~0x400000ULL)==pthread_flags)
            return NAS_SC_clone_thread;
        /* Original Go age-keygen uses a shared Linux thread with no TID
         * pointer side effects: 0x50f00 on AArch64 and 0xd0f00 on x86-64.
         * The latter sets CLONE_SETTLS. The absence of SETTLS on ARM means
         * arg4 is NOT a TLS value even when nonzero. Do not accept unrelated
         * flags or assume pthread's PARENT_SETTID/CHILD_CLEARTID semantics. */
        if (args[1]==0x00050f00ULL || args[1]==0x000d0f00ULL)
            return NAS_SC_clone_thread;
        if (args[1] == (0x00000100ULL|0x00004000ULL|17ULL))
            return NAS_SC_vfork;
        if (args[2] != 0U) return NAS_SC_NONE; /* regular fork inherits sp */
        if (args[1] == 17U) return NAS_SC_fork;
        /* Fork with child TID set/clear, no shared VM or alternate stack.
         * Caller translates child_tid from architecture-specific registers. */
        if (args[1] == (0x01000000ULL|0x00200000ULL|17U) &&
            nas_linux_clone_child_tid(args) != 0)
            return NAS_SC_fork_child_tid;
        return NAS_SC_NONE;
    }
    return nas_linux_decode_syscall(args[0]);
}
#endif
