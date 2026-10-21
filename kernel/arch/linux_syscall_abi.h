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
 * Generic64 clone(220) maps to the EXISTING fork operation ONLY for the
 * exactly supported Linux fork-equivalent form (SIGCHLD, null child stack).
 * Threads/CLONE_VM/CLONE_VFORK and unknown flags remain unsupported.
 * Unsupported/legacy syscalls map to NONE and are reported as -ENOSYS. */
#if defined(__x86_64__)
# include "../nr.h" /* pinned native x86 table; only the decoder includes it */
#elif !(defined(__aarch64__) || (defined(__riscv) && __riscv_xlen == 64))
# error "Linux syscall ABI decoder required for this architecture"
#endif
static inline enum nas_syscall_op nas_linux_decode_syscall(uint64_t number) {
    switch (number) {
#if defined(__x86_64__)
    case __NR_arch_prctl: return NAS_SC_arch_prctl;
    case __NR_brk: return NAS_SC_brk;
    case __NR_chdir: return NAS_SC_chdir;
    case __NR_close: return NAS_SC_close;
    case __NR_dup: return NAS_SC_dup;
    case __NR_dup2: return NAS_SC_dup2;
    case __NR_dup3: return NAS_SC_dup3;
    case __NR_execve: return NAS_SC_execve;
    case __NR_exit: return NAS_SC_exit;
    case __NR_exit_group: return NAS_SC_exit_group;
    case __NR_fchdir: return NAS_SC_fchdir;
    case __NR_fcntl: return NAS_SC_fcntl;
    case __NR_fork: return NAS_SC_fork;
    case __NR_fstat: return NAS_SC_fstat;
    case __NR_getcwd: return NAS_SC_getcwd;
    case __NR_getdents64: return NAS_SC_getdents64;
    case __NR_getegid: return NAS_SC_getegid;
    case __NR_geteuid: return NAS_SC_geteuid;
    case __NR_getgid: return NAS_SC_getgid;
    case __NR_getpgid: return NAS_SC_getpgid;
    case __NR_getpgrp: return NAS_SC_getpgrp;
    case __NR_getpid: return NAS_SC_getpid;
    case __NR_getppid: return NAS_SC_getppid;
    case __NR_gettid: return NAS_SC_gettid;
    case __NR_getuid: return NAS_SC_getuid;
    case __NR_ioctl: return NAS_SC_ioctl;
    case __NR_lseek: return NAS_SC_lseek;
    case __NR_lstat: return NAS_SC_lstat;
    case __NR_mkdir: return NAS_SC_mkdir;
    case __NR_mkdirat: return NAS_SC_mkdirat;
    case __NR_mmap: return NAS_SC_mmap;
    case __NR_mprotect: return NAS_SC_mprotect;
    case __NR_munmap: return NAS_SC_munmap;
    case __NR_newfstatat: return NAS_SC_newfstatat;
    case __NR_open: return NAS_SC_open;
    case __NR_openat: return NAS_SC_openat;
    case __NR_pipe: return NAS_SC_pipe;
    case __NR_pipe2: return NAS_SC_pipe2;
    case __NR_poll: return NAS_SC_poll;
    case __NR_read: return NAS_SC_read;
    case __NR_readlink: return NAS_SC_readlink;
    case __NR_readlinkat: return NAS_SC_readlinkat;
    case __NR_rename: return NAS_SC_rename;
    case __NR_renameat: return NAS_SC_renameat;
    case __NR_renameat2: return NAS_SC_renameat2;
    case __NR_rmdir: return NAS_SC_rmdir;
    case __NR_rt_sigaction: return NAS_SC_rt_sigaction;
    case __NR_rt_sigprocmask: return NAS_SC_rt_sigprocmask;
    case __NR_set_tid_address: return NAS_SC_set_tid_address;
    case __NR_stat: return NAS_SC_stat;
    case __NR_uname: return NAS_SC_uname;
    case __NR_unlink: return NAS_SC_unlink;
    case __NR_unlinkat: return NAS_SC_unlinkat;
    case __NR_wait4: return NAS_SC_wait4;
    case __NR_write: return NAS_SC_write;
    case __NR_writev: return NAS_SC_writev;
#else
    case 63U: return NAS_SC_read;
    case 64U: return NAS_SC_write;
    case 66U: return NAS_SC_writev;
    case 56U: return NAS_SC_openat;
    case 57U: return NAS_SC_close;
    case 62U: return NAS_SC_lseek;
    case 80U: return NAS_SC_fstat;
    case 79U: return NAS_SC_newfstatat;
    case 61U: return NAS_SC_getdents64;
    case 78U: return NAS_SC_readlinkat;
    case 50U: return NAS_SC_fchdir;
    case 17U: return NAS_SC_getcwd;
    case 59U: return NAS_SC_pipe2;
    case 25U: return NAS_SC_fcntl;
    case 23U: return NAS_SC_dup;
    case 24U: return NAS_SC_dup3;
    case 49U: return NAS_SC_chdir;
    case 29U: return NAS_SC_ioctl;
    case 38U: return NAS_SC_renameat;
    case 276U: return NAS_SC_renameat2;
    case 34U: return NAS_SC_mkdirat;
    case 35U: return NAS_SC_unlinkat;
    case 134U: return NAS_SC_rt_sigaction;
    case 135U: return NAS_SC_rt_sigprocmask;
    case 160U: return NAS_SC_uname;
    case 96U: return NAS_SC_set_tid_address;
    case 172U: return NAS_SC_getpid;
    case 173U: return NAS_SC_getppid;
    case 174U: return NAS_SC_getuid;
    case 175U: return NAS_SC_geteuid;
    case 176U: return NAS_SC_getgid;
    case 177U: return NAS_SC_getegid;
    case 178U: return NAS_SC_gettid;
    case 155U: return NAS_SC_getpgid;
    case 222U: return NAS_SC_mmap;
    case 215U: return NAS_SC_munmap;
    case 226U: return NAS_SC_mprotect;
    case 214U: return NAS_SC_brk;
    case 260U: return NAS_SC_wait4;
    case 221U: return NAS_SC_execve;
    case 93U: return NAS_SC_exit;
    case 94U: return NAS_SC_exit_group;
#endif
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
static inline enum nas_syscall_op nas_linux_decode_call(const uint64_t *args) {
    if (!args) return NAS_SC_NONE;
#if defined(__x86_64__)
    const uint64_t clone_nr = __NR_clone;
#else
    const uint64_t clone_nr = 220U; /* Linux asm-generic __NR_clone */
#endif
    if (args[0] == clone_nr) {
        if (args[2] != 0U) return NAS_SC_NONE; /* no alternate user stack */
        if (args[1] == 17U) return NAS_SC_fork;
#if !defined(__x86_64__)
        /* Observed directly from original Debian riscv64 BusyBox ash on
         * real QEMU: CLONE_CHILD_SETTID|CLONE_CHILD_CLEARTID|SIGCHLD.
         * No CLONE_VM/CLONE_THREAD/CLONE_VFORK, so this is an actual fork
         * with per-child TID set/clear, NOT an approximated vfork/thread.
         * Reject unknown flags rather than silently weakening semantics. */
        if (args[1] == (0x01000000ULL|0x00200000ULL|17U) &&
            args[5] != 0)
            return NAS_SC_fork_child_tid;
#endif
        return NAS_SC_NONE;
    }
    return nas_linux_decode_syscall(args[0]);
}
#endif
