#include <stdint.h>
#include <stdbool.h>
#include "dispatch.h"
#include "../syscall/ops.h"
#include "../arch/linux_syscall_abi.h"
#include "../errno.h"
#include "../task.h"
#include "../fd.h"
#include "../arch/arch.h"
#include "../arch/abi.h"
#include "../mm/vm.h"
#include "../mm/usercopy.h"
#include "../task/exec.h"
#include "../task/process.h"
#include "../task/wait_pid.h"
#include "../task/sched.h"
#include "../task/signal.h"
#include "../task/user_stack.h"
#include "../fs/io.h"
#include "../fs/syscall.h"
#include "../platform/platform.h"
#include "../drivers/serial.h"
#include "../panic.h"
#define TASK (*nas_current_task)
#define thread_clear_tid TASK.thread_clear_tid
#define descriptors TASK.fds.descriptors
#define file_slot TASK.fds.file_slot
#define file_status_flags nas_file_status_flags
#define running_bash TASK.running_bash
#define running_busybox TASK.running_busybox
#define running_alpine TASK.running_alpine
#define saw_bash_hello TASK.saw_bash_hello

static uint32_t missing_count[512];
static uint64_t sys_uname_naseed(uint64_t buffer) {
    struct uts {
        char sysname[65], node[65], release[65], version[65];
        char machine[65], domain[65];
    };
    static const struct uts data = {
        "Linux", "naseed", "0.1", "NaSeed experimental",
        NAS_UTS_MACHINE, "localdomain"
    };
    return nas_user_write_bytes(buffer, (const uint8_t *)&data, sizeof(data))
        ? 0 : (uint64_t)-EFAULT;
}


static uint64_t task_pipe_io(uint64_t *frame, uint64_t nr,
                             uint64_t fd, uint64_t buf, uint64_t count) {
    uint64_t ret;
    if (nr == NAS_SC_read) ret = nas_fs_fd_read(fd, buf, count);
    else if (nr == NAS_SC_write) ret = nas_fs_fd_write(fd, buf, count);
    else ret = nas_fs_fd_writev(fd, buf, count);
    if ((int64_t)ret == -EPIPE &&
        (nr == NAS_SC_write || nr == NAS_SC_writev) &&
        nas_sched_sigpipe_default(fd)) {
        nas_sched_child_exit(0, NAS_SIGPIPE);
        return nas_task_schedule(frame);
    }
    if ((int64_t)ret == -EOPNOTSUPP && fd < NAS_FD_LIMIT &&
        (descriptors[fd] == FD_PIPE_R || descriptors[fd] == FD_PIPE_W) &&
        !(file_status_flags[file_slot[fd]] & 0x800ULL))
        return nas_task_block(frame, nr, fd, buf, count, 0);
    return ret;
}

/* Strict single-program ABI diagnostic dispatcher, for a real syscall gate. */
uint64_t syscall_dispatch(const uint64_t *args) {
    const uint64_t raw_nr = args[0];
    enum nas_syscall_op nr = nas_linux_decode_call(args);
#if defined(__riscv) && __riscv_xlen == 64
    /* Native BusyBox ash may invoke clone with Linux flags other than
     * SIGCHLD. Do not pretend this is fork: show the actual ABI contract
     * before implementing vfork/clone semantics in the existing task core. */
    if (raw_nr == 220U && nr == NAS_SC_NONE) {
        nas_print("[RV64 CLONE] unsupported flags=");
        nas_hex(args[1]);
        nas_print(" child_stack="); nas_hex(args[2]);
        nas_print(" parent_tid="); nas_hex(args[3]);
        nas_print(" tls="); nas_hex(args[4]);
        nas_print(" child_tid="); nas_hex(args[5]);
        nas_print("\n");
    }
#endif
    /* Real-program execution trace: syscall identity, callsite and args.
     * Capped to avoid serial storms; not needed in exception context.
     */
#ifdef NASEED_SYSCALL_TRACE
    static unsigned trace_count;
    if ((running_bash || running_busybox) && trace_count++ < 96) {
        nas_print("[TRACE] nr="); nas_dec(nr);
        nas_print(" rip="); nas_hex(args[7]);
        nas_print(" rsp="); nas_hex(syscall_user_rsp);
        nas_print(" a1="); nas_hex(args[1]);
        nas_print(" a2="); nas_hex(args[2]);
        nas_print(" a3="); nas_hex(args[3]);
        nas_print("\n");
    }
#endif
    if (nr == NAS_SC_arch_prctl) return nas_linux_arch_prctl(args[1], args[2]);
    if (nr == NAS_SC_set_tid_address) { thread_clear_tid = args[1]; return TASK.pid; }
    if (nr == NAS_SC_getpid || nr == NAS_SC_gettid) return TASK.pid;
    if (nr == NAS_SC_getppid) return TASK.ppid;
    if (nr == NAS_SC_getuid || nr == NAS_SC_getgid || nr == NAS_SC_geteuid || nr == NAS_SC_getegid) return 0; /* root credentials */
    if (nr == NAS_SC_mprotect) return nas_vm_mprotect(args[1], args[2], args[3]);
    if (nr == NAS_SC_mmap) return nas_vm_anon_mmap(args[1], args[2], args[3],
                                       args[4], args[5], args[6]);
    if (nr == NAS_SC_munmap) return nas_vm_munmap(args[1], args[2]);
    if (nr == NAS_SC_brk) return nas_vm_brk(args[1]);
    if (nr == NAS_SC_read) return task_pipe_io((uint64_t *)(uintptr_t)args, nr, args[1], args[2], args[3]);
    if (nr == NAS_SC_write) return task_pipe_io((uint64_t *)(uintptr_t)args, nr, args[1], args[2], args[3]);
    if (nr == NAS_SC_writev) return task_pipe_io((uint64_t *)(uintptr_t)args, nr, args[1], args[2], args[3]);
    struct nas_fs_result fs_result = nas_fs_syscall(nr, args);
    if (fs_result.handled) return fs_result.value;
    if (nr == NAS_SC_rt_sigaction) return nas_signal_action(args[1], args[2], args[3], args[4]);
    if (nr == NAS_SC_rt_sigprocmask) return nas_signal_procmask(args[1], args[2], args[3], args[4]);
    if (nr == NAS_SC_uname) return sys_uname_naseed(args[1]);

    if (nr == NAS_SC_fork)
        return nas_process_fork((uint64_t *)(uintptr_t)args);
    if (nr == NAS_SC_fork_child_tid)
        return nas_process_fork_child_tid((uint64_t *)(uintptr_t)args,
                                          nas_linux_clone_child_tid(args));

    if (nr == NAS_SC_wait4) {
        uint64_t ret = nas_process_wait4(args[1], args[2], args[3], args[4]);
        if ((int64_t)ret == -EOPNOTSUPP && args[4] == 0 &&
            args[3] == 0 && nas_wait_pid_supported(args[1]))
            return nas_task_block((uint64_t *)(uintptr_t)args, nr,
                              args[1], args[2], args[3], args[4]);
        return ret;
    }
    if (nr == NAS_SC_execve) return nas_task_execve(args[1], args[2], args[3]);
    if (nr == NAS_SC_getpgid) return args[1] == 0 || args[1] == TASK.pid ? TASK.pid : (uint64_t)-ESRCH;
    if (nr == NAS_SC_getpgrp) return TASK.pid; /* one process group today */
    if (nr == NAS_SC_exit || nr == NAS_SC_exit_group) { /* exit / exit_group */
        if (TASK.ppid) {
            nas_sched_child_exit((uint32_t)args[1], 0);
            return nas_task_schedule((uint64_t *)(uintptr_t)args);
        }
        nas_process_clear_tid_on_exit();
        TASK.exit_code = (uint32_t)(args[1] & 255ULL);
        TASK.termination_signal = 0;
        TASK.state = NAS_TASK_ZOMBIE;
        nas_print("[EXIT] user status="); nas_dec(args[1]); nas_print("\n");
        /* In cmdline-driven tests, the host verifies real stdout and status.
         * Do not encode applet-specific success checks into the kernel.
         * Legacy M0-M4 tests keep their strict hello-specific acceptance.
         */
        if (nas_boot_argv_enabled()) {
            if (args[1] != 0) nas_fail("cmdline userspace exited nonzero");
            nas_print("[PASS] cmdline userspace exit 0\n");
            nas_platform_debug_exit(0);
            nas_fail("QEMU debug exit returned unexpectedly");
        }
        if (running_busybox) {
            if (args[1] == 0 && saw_bash_hello) {
                if (running_alpine)
                    nas_print("[PASS] M4 Alpine dynamic BusyBox echo hello\n");
                else
                    nas_print("[PASS] M2 Debian BusyBox echo hello\n");
                nas_platform_debug_exit(0);
            }
            nas_fail("real BusyBox exited without stdout hello and status 0");
        }
        if (running_bash) {
            if (args[1] == 0 && saw_bash_hello) {
                nas_print("[PASS] M2 real GNU Bash -c echo hello\n");
                nas_platform_debug_exit(0);
            }
            nas_fail("real Bash exited without exact hello and status zero");
        }
        if (args[1] == 0) {
            nas_print("[PASS] M1 ELF / ring3 / syscall write / ENOSYS / exit\n");
            nas_platform_debug_exit(0);
        }
        nas_fail("user program exit was non-zero");
    }
    if (raw_nr >= sizeof(missing_count) / sizeof(missing_count[0]) ||
        ++missing_count[raw_nr] <= 3) {
        nas_print("[MISS] syscall nr="); nas_dec(raw_nr); nas_print("\n");
    }
    return (uint64_t)-ENOSYS;
}
