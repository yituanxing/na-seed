
#include <limine.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "nr.h"
#include "errno.h"
#include "bootfs.h"
#include "task.h"
#include "fd.h"
#include "tmpfs.h"
#include "path.h"
#include "pipe.h"
#include "boot_image.h"
#include "arch/arch.h"
#include "mm/vm.h"
#include "mm/usercopy.h"
#include "task/process.h"
#include "task/sched.h"
#include "task/elf.h"
#include "task/user_stack.h"
#include "task/signal.h"
#include "arch/x86_64/syscall.h"
#include "fs/io.h"
#include "fs/syscall.h"
#include "platform/platform.h"

/* Process-local state was previously static global memory. A task is now
 * its explicit owner; the single-task aliases keep the proven syscall path
 * unchanged while the upcoming arch task switch is developed separately.
 */
#define TASK (*nas_current_task)
#define root_phys TASK.root_phys
#define heap_break TASK.heap_break
#define mmap_cursor TASK.mmap_cursor
#define loaded_phdr TASK.loaded_phdr
#define loaded_phnum TASK.loaded_phnum
#define loaded_main_entry TASK.loaded_main_entry
#define loaded_interp_base TASK.loaded_interp_base
#define thread_clear_tid TASK.thread_clear_tid
#define user_fs_base TASK.arch.user_fs_base
#define descriptors TASK.fds.descriptors
#define file_slot TASK.fds.file_slot
#define file_refcount nas_file_refcount
#define fd_cloexec TASK.fds.fd_cloexec
#define file_status_flags nas_file_status_flags
#define boot_files TASK.fds.boot_files
#define boot_entries TASK.fds.boot_entries
#define boot_file_offsets nas_boot_file_offsets
#define current_workdir TASK.cwd
#define blocked_signals TASK.blocked_signals
#define signal_actions TASK.signal_actions
#define running_bash TASK.running_bash
#define running_busybox TASK.running_busybox
#define running_alpine TASK.running_alpine
#define saw_bash_hello TASK.saw_bash_hello
#define matched_chars TASK.matched_chars

/* Single-core M1: run a REAL static ELF from a Limine module in ring 3.
 * The new process CR3 preserves Limine's supervisor upper-half/HHDM mappings.
 * Private user address spaces and cooperative task switching are present.
 * Timer preemption and independent per-task kernel stacks are not.
 * Interrupts are disabled on syscall entry; TSS handles ring3 faults.
 */
extern uint64_t nas_frame_alloc(void);
extern bool nas_frame_free(uint64_t physical);
extern uint64_t nas_hhdm_offset(void);
extern void nas_print(const char *);
extern void nas_dec(uint64_t);
extern void nas_hex(uint64_t);
extern const struct limine_file *nas_user_module(void);
extern const struct limine_file *nas_module(uint64_t index);

#define USER_STACK_TOP 0x0000000070000000ULL
#define USER_STACK_PAGES 32

__attribute__((noreturn)) static void fail(const char *message) {
    nas_print("[FAIL] M1 ");
    nas_print(message);
    nas_print("\n");
    nas_arch_halt();
}
static uint32_t missing_count[512];
/* Boot/exec program-name comparison. FD path matching lives in fs/syscall.c. */
static bool same_string(const char *a, const char *b) {
    for (; *a && *b; a++, b++) if (*a != *b) return false;
    return *a == *b;
}
static uint64_t sys_uname_naseed(uint64_t buffer) {
    struct uts {
        char sysname[65], node[65], release[65], version[65];
        char machine[65], domain[65];
    };
    static const struct uts data = {
        "Linux", "naseed", "0.1", "NaSeed experimental x86_64",
        "x86_64", "localdomain"
    };
    return nas_user_write_bytes(buffer, (const uint8_t *)&data, sizeof(data))
        ? 0 : (uint64_t)-EFAULT;
}

/* Single-process execve: replace this process's user image, not fork.
 * Snapshot ALL user pointers before changing CR3. The ELF and interpreter
 * bytes are immutable CPIO entries, so they remain valid after replacement.
 * Stage image in a fresh root, rollback if load fails; only on success
 * close CLOEXEC descriptors, release old user pages and re-enter Ring 3.
 */
#define EXEC_STRING_MAX 256
static struct exec_snapshot {
    char filename[256];
    char argbuf[NAS_ARGV_MAX][EXEC_STRING_MAX];
    char envbuf[NAS_ENV_MAX][EXEC_STRING_MAX];
    const char *argv[NAS_ARGV_MAX];
    const char *envp[NAS_ENV_MAX];
    unsigned argc, envc;
    /* #! interpreter and its single optional argument survive user CR3
     * replacement; never retain pointers into a CPIO line buffer. */
    char shebang_path[256], shebang_arg[256];
} pending_exec;

/* Linux binfmt_script: interpret a bounded CPIO #! line as an ELF
 * interpreter plus one optional argument. The script itself is NOT mapped
 * as ELF and argv[0] of the original caller is replaced by the interpreter.
 * No recursive shebangs or Linux-binfmt registrations are pretended.
 */
static int read_shebang(const struct elf_image *image,
                        char interpreter[256], char option[256]) {
    if (!image || image->size < 3 || image->data[0] != '#' ||
        image->data[1] != '!') return -ENOEXEC;
    unsigned end = 2;
    while (end < image->size && end < 256 &&
           image->data[end] != '\n' && image->data[end] != 0)
        end++;
    /* Reject a truncated first line rather than guessing an interpreter. */
    if (end == 256 || end == image->size || image->data[end] == 0)
        return -ENOEXEC;
    unsigned i = 2;
    while (i < end && (image->data[i] == ' ' || image->data[i] == '\t'))
        i++;
    unsigned start = i;
    while (i < end && image->data[i] != ' ' && image->data[i] != '\t' &&
           image->data[i] != '\r')
        i++;
    if (i == start || image->data[start] != '/') return -ENOEXEC;
    unsigned n = i - start;
    for (unsigned j = 0; j < n; j++)
        interpreter[j] = (char)image->data[start + j];
    interpreter[n] = 0;
    while (i < end && (image->data[i] == ' ' || image->data[i] == '\t'))
        i++;
    start = i;
    while (end > start && (image->data[end - 1] == ' ' ||
                            image->data[end - 1] == '\t' ||
                            image->data[end - 1] == '\r'))
        end--;
    n = end - start;
    for (unsigned j = 0; j < n; j++)
        option[j] = (char)image->data[start + j];
    option[n] = 0;
    return 0;
}
static uint64_t shebang_rewrite_args(void) {
    /* argv[0] is replaced, not appended. Preserve argv[1..] and envp. */
    const bool option = pending_exec.shebang_arg[0] != 0;
    unsigned original = pending_exec.argc;
    if (original + 1 + (option ? 1U : 0U) > NAS_ARGV_MAX)
        return (uint64_t)-E2BIG;
    const char *rebuilt[NAS_ARGV_MAX];
    unsigned n = 0;
    rebuilt[n++] = pending_exec.shebang_path;
    if (option) rebuilt[n++] = pending_exec.shebang_arg;
    rebuilt[n++] = pending_exec.filename;
    for (unsigned i = 1; i < original; i++)
        rebuilt[n++] = pending_exec.argv[i];
    for (unsigned i = 0; i < n; i++) pending_exec.argv[i] = rebuilt[i];
    pending_exec.argc = n;
    return 0;
}

static uint64_t copy_exec_string(uint64_t addr, char dst[EXEC_STRING_MAX]) {
    if (addr >= USER_LIMIT) return (uint64_t)-EFAULT;
    for (unsigned i = 0; i < EXEC_STRING_MAX; i++) {
        uint8_t ch;
        if (!nas_user_read_bytes(addr + i, &ch, 1)) return (uint64_t)-EFAULT;
        dst[i] = (char)ch;
        if (!ch) return 0;
    }
    return (uint64_t)-E2BIG;
}
static uint64_t copy_exec_vector(uint64_t user_array, bool environment) {
    unsigned limit = environment ? NAS_ENV_MAX : NAS_ARGV_MAX;
    if (!user_array) {
        if (!environment) {
            pending_exec.argv[0] = pending_exec.argbuf[0];
            pending_exec.argbuf[0][0] = 0;
            pending_exec.argc = 1; /* argc>=1 Linux compatibility */
        }
        return 0;
    }
    for (unsigned i = 0; i <= limit; i++) {
        uint64_t value;
        if (user_array >= USER_LIMIT ||
            i > (USER_LIMIT - user_array) / 8 ||
            !nas_user_read_u64(user_array + i * 8, &value))
            return (uint64_t)-EFAULT;
        if (!value) {
            if (environment) pending_exec.envc = i;
            else {
                pending_exec.argc = i;
                if (!i) {
                    pending_exec.argc = 1;
                    pending_exec.argv[0] = pending_exec.argbuf[0];
                    pending_exec.argbuf[0][0] = 0;
                }
            }
            return 0;
        }
        if (i == limit) return (uint64_t)-E2BIG;
        char *storage = environment ? pending_exec.envbuf[i] : pending_exec.argbuf[i];
        uint64_t ret = copy_exec_string(value, storage);
        if ((int64_t)ret < 0) return ret;
        if (environment) pending_exec.envp[i] = storage;
        else pending_exec.argv[i] = storage;
    }
    return (uint64_t)-E2BIG;
}
static uint64_t sys_execve(uint64_t pathname, uint64_t argv, uint64_t envp) {
    if (!bootfs_mounted()) return (uint64_t)-ENOENT;
    uint64_t result = nas_fs_resolve_path((uint64_t)-100, pathname, pending_exec.filename);
    if ((int64_t)result < 0) return result;
    char actual[256];
    int err = bootfs_resolve(pending_exec.filename, true, actual);
    if (err) return (uint64_t)(int64_t)err;
    const struct bootfs_entry *app = bootfs_find(actual);
    if (bootfs_is_dir(app) || !(app->mode & 0111U))
        return (uint64_t)-EACCES;
    struct elf_image main_img = { app->data, app->size };
    bool scripted = false;
    if (main_img.size >= 2 && main_img.data[0] == '#' &&
        main_img.data[1] == '!') {
        err = read_shebang(&main_img, pending_exec.shebang_path,
                           pending_exec.shebang_arg);
        if (err) return (uint64_t)(int64_t)err;
        char resolved_interpreter[256];
        err = bootfs_resolve(pending_exec.shebang_path, true,
                             resolved_interpreter);
        if (err) return (uint64_t)(int64_t)err;
        const struct bootfs_entry *bin = bootfs_find(resolved_interpreter);
        if (!bin) return (uint64_t)-ENOENT;
        if (bootfs_is_dir(bin) || !(bin->mode & 0111U))
            return (uint64_t)-EACCES;
        main_img = (struct elf_image){ bin->data, bin->size };
        unsigned n = 0;
        do { actual[n] = resolved_interpreter[n]; }
        while (resolved_interpreter[n++]);
        scripted = true;
    }
    /* Scripts are accepted only through an actual executable ELF
     * interpreter; never claim a nested or invalid #! succeeds. */
    if (main_img.size < sizeof(struct elf_hdr) ||
        main_img.data[0] != 0x7f || main_img.data[1] != 'E' ||
        main_img.data[2] != 'L' || main_img.data[3] != 'F')
        return (uint64_t)-ENOEXEC;
    const struct bootfs_entry *loader = bootfs_find("/lib/ld-musl-x86_64.so.1");
    struct elf_image interp_img = {0};
    const struct elf_image *interp = 0;
    if (loader && !bootfs_is_dir(loader)) {
        interp_img = (struct elf_image){loader->data, loader->size};
        interp = &interp_img;
    }
    result = copy_exec_vector(argv, false);
    if ((int64_t)result < 0) return result;
    result = copy_exec_vector(envp, true);
    if ((int64_t)result < 0) return result;
    if (scripted) {
        result = shebang_rewrite_args();
        if ((int64_t)result < 0) return result;
    }
    uint64_t new_root = nas_mmu_alloc_root();
    if (!new_root) return (uint64_t)-ENOMEM;
    uint64_t previous_root = root_phys;
    uint64_t old_heap = heap_break, old_cursor = mmap_cursor;
    uint64_t old_phdr = loaded_phdr, old_phnum = loaded_phnum;
    uint64_t old_entry = loaded_main_entry, old_interp = loaded_interp_base;
    nas_mmu_switch_root(new_root);
    heap_break = 0;
    mmap_cursor = 0x60000000ULL;
    loaded_phdr = loaded_phnum = loaded_main_entry = loaded_interp_base = 0;
    uint64_t entry, sp = 0;
    bool good = nas_elf_load(&main_img, interp, &entry);
    if (good) {
        for (uint64_t va = USER_STACK_TOP - USER_STACK_PAGES * PAGE;
             va < USER_STACK_TOP; va += PAGE)
            if (!nas_mmu_map_page(va, true)) { good = false; break; }
    }
    if (good)
        good = nas_build_user_stack(pending_exec.argv, pending_exec.argc,
                                pending_exec.envp, pending_exec.envc,
                                pending_exec.filename, &sp);
    if (!good) {
        nas_mmu_switch_root(previous_root);
        if (!nas_mmu_reclaim_root(new_root)) fail("execve rollback page-table corruption");
        heap_break = old_heap;
        mmap_cursor = old_cursor;
        loaded_phdr = old_phdr;
        loaded_phnum = old_phnum;
        loaded_main_entry = old_entry;
        loaded_interp_base = old_interp;
        return (uint64_t)-ENOEXEC;
    }
    /* Commit: syscall can no longer return to old userspace RIP or RSP. */
    for (unsigned fd = 0; fd < 64; fd++)
        if (descriptors[fd] != FD_CLOSED && fd_cloexec[fd])
            nas_fs_close_fd(fd);
    thread_clear_tid = 0;
    user_fs_base = 0;
    nas_arch_set_user_tls(0); /* fresh musl/glibc TLS initialization */
    for (unsigned i = 0; i < 65; i++)
        for (unsigned j = 0; j < 4; j++)
            signal_actions[i][j] = 0; /* exec resets user signal handlers */
    if (!nas_mmu_reclaim_root(previous_root))
        fail("execve stale address-space frame reclaim failure");
    const char *basename = actual;
    for (const char *p = basename; *p; p++)
        if (*p == '/') basename = p + 1;
    running_busybox = same_string(basename, "busybox");
    running_bash = same_string(basename, "bash");
    running_alpine = running_busybox &&
        ((const struct elf_hdr *)main_img.data)->type == 3;
    if (scripted) {
        nas_print("[EXEC] CPIO #! interpreter: ");
        nas_print(pending_exec.shebang_path); nas_print("\n");
    }
    nas_print("[EXEC] replaced process with CPIO ELF: ");
    nas_print(pending_exec.filename);
    nas_print("\n");
    nas_arch_enter_user(entry, sp);
    fail("execve iretq returned unexpectedly");
}


static uint64_t task_pipe_io(uint64_t *frame, uint64_t nr,
                             uint64_t fd, uint64_t buf, uint64_t count) {
    uint64_t ret;
    if (nr == __NR_read) ret = nas_fs_fd_read(fd, buf, count);
    else if (nr == __NR_write) ret = nas_fs_fd_write(fd, buf, count);
    else ret = nas_fs_fd_writev(fd, buf, count);
    if ((int64_t)ret == -EPIPE &&
        (nr == __NR_write || nr == __NR_writev) &&
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
    uint64_t nr = args[0];
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
    if (nr == __NR_arch_prctl) return nas_arch_prctl(args[1], args[2]);
    if (nr == __NR_set_tid_address) { thread_clear_tid = args[1]; return TASK.pid; }
    if (nr == __NR_getpid || nr == __NR_gettid) return TASK.pid;
    if (nr == __NR_getppid) return TASK.ppid;
    if (nr == __NR_getuid || nr == __NR_getgid || nr == __NR_geteuid || nr == __NR_getegid) return 0; /* root credentials */
    if (nr == __NR_mprotect) return nas_vm_mprotect(args[1], args[2], args[3]);
    if (nr == __NR_mmap) return nas_vm_anon_mmap(args[1], args[2], args[3],
                                       args[4], args[5], args[6]);
    if (nr == __NR_munmap) return nas_vm_munmap(args[1], args[2]);
    if (nr == __NR_brk) return nas_vm_brk(args[1]);
    if (nr == __NR_read) return task_pipe_io((uint64_t *)(uintptr_t)args, nr, args[1], args[2], args[3]);
    if (nr == __NR_write) return task_pipe_io((uint64_t *)(uintptr_t)args, nr, args[1], args[2], args[3]);
    if (nr == __NR_writev) return task_pipe_io((uint64_t *)(uintptr_t)args, nr, args[1], args[2], args[3]);
    struct nas_fs_result fs_result = nas_fs_syscall(args);
    if (fs_result.handled) return fs_result.value;
    if (nr == __NR_rt_sigaction) return nas_signal_action(args[1], args[2], args[3], args[4]);
    if (nr == __NR_rt_sigprocmask) return nas_signal_procmask(args[1], args[2], args[3], args[4]);
    if (nr == __NR_uname) return sys_uname_naseed(args[1]);

    if (nr == __NR_fork) return nas_process_fork((uint64_t *)(uintptr_t)args);

    if (nr == __NR_wait4) {
        uint64_t ret = nas_process_wait4(args[1], args[2], args[3], args[4]);
        if ((int64_t)ret == -EOPNOTSUPP && args[4] == 0 &&
            args[3] == 0 && ((int64_t)args[1] == -1 || (int64_t)args[1] > 0))
            return nas_task_block((uint64_t *)(uintptr_t)args, nr,
                              args[1], args[2], args[3], args[4]);
        return ret;
    }
    if (nr == __NR_execve) return sys_execve(args[1], args[2], args[3]);
    if (nr == __NR_getpgid) return args[1] == 0 || args[1] == TASK.pid ? TASK.pid : (uint64_t)-ESRCH;
    if (nr == __NR_getpgrp) return TASK.pid; /* one process group today */
    if (nr == __NR_exit || nr == __NR_exit_group) { /* exit / exit_group */
        if (TASK.ppid) {
            nas_sched_child_exit((uint32_t)args[1], 0);
            return nas_task_schedule((uint64_t *)(uintptr_t)args);
        }
        TASK.exit_code = (uint32_t)(args[1] & 255ULL);
        TASK.termination_signal = 0;
        TASK.state = NAS_TASK_ZOMBIE;
        nas_print("[EXIT] user status="); nas_dec(args[1]); nas_print("\n");
        /* In cmdline-driven tests, the host verifies real stdout and status.
         * Do not encode applet-specific success checks into the kernel.
         * Legacy M0-M4 tests keep their strict hello-specific acceptance.
         */
        if (nas_boot_argv_enabled()) {
            if (args[1] != 0) fail("cmdline userspace exited nonzero");
            nas_print("[PASS] cmdline userspace exit 0\n");
            nas_platform_debug_exit(0);
            fail("QEMU debug exit returned unexpectedly");
        }
        if (running_busybox) {
            if (args[1] == 0 && saw_bash_hello) {
                if (running_alpine)
                    nas_print("[PASS] M4 Alpine dynamic BusyBox echo hello\n");
                else
                    nas_print("[PASS] M2 Debian BusyBox echo hello\n");
                nas_platform_debug_exit(0);
            }
            fail("real BusyBox exited without stdout hello and status 0");
        }
        if (running_bash) {
            if (args[1] == 0 && saw_bash_hello) {
                nas_print("[PASS] M2 real GNU Bash -c echo hello\n");
                nas_platform_debug_exit(0);
            }
            fail("real Bash exited without exact hello and status zero");
        }
        if (args[1] == 0) {
            nas_print("[PASS] M1 ELF / ring3 / syscall write / ENOSYS / exit\n");
            nas_platform_debug_exit(0);
        }
        fail("user program exit was non-zero");
    }
    if (nr >= sizeof(missing_count) / sizeof(missing_count[0]) ||
        ++missing_count[nr] <= 3) {
        nas_print("[MISS] syscall nr="); nas_dec(nr); nas_print("\n");
    }
    return (uint64_t)-ENOSYS;
}

void m1_start(void) {
    nas_print("[M1] setting up GDT, TSS, IDT, syscall MSRs\n");
    nas_task_boot_init();
    nas_arch_cpu_tables_init();
    nas_arch_cpu_user_fp_init();
    if (!nas_mmu_bootstrap()) fail("requires four-level paging");
    nas_arch_syscall_enable();
    /* Select the boot ELF with the very same argv[0] passed to userspace.
     * No shell, PATH lookup, or alternative program-name ABI is invented.
     * Legacy M0-M5 configurations without cmdline preserve their defaults.
     */
    extern const char *nas_cmdline(void);
    const char *cmdline = nas_cmdline();
    if (cmdline && *cmdline) {
        const char *const *args;
        unsigned argc;
        if (!nas_parse_boot_argv(cmdline, &args, &argc))
            fail("invalid Limine naseed.argv configuration");
    }
    /* File-system bytes and the selected executable are separate:
     * boot_image.c recognizes the Limine handoff and indexes CPIO via
     * bootfs.c; only this layer maps the actual ELF into userspace.
     */
    struct nas_boot_image selected_image;
    const char *boot_error = nas_boot_prepare(
        nas_boot_argv_first(), &selected_image);
    if (boot_error) fail(boot_error);
    struct elf_image main_image = {
        selected_image.image, selected_image.size
    };
    struct elf_image interp_image = {
        selected_image.interpreter, selected_image.interpreter_size
    };
    const struct elf_image *interpreter =
        selected_image.has_interpreter ? &interp_image : 0;
    running_bash = selected_image.bash;
    running_busybox = selected_image.busybox;
    running_alpine = selected_image.alpine;
    uint64_t entry = 0;
    if (!nas_elf_load(&main_image, interpreter, &entry))
        fail("invalid or unmappable userspace ELF");
    for (uint64_t va = USER_STACK_TOP - USER_STACK_PAGES * PAGE;
         va < USER_STACK_TOP; va += PAGE) {
        if (!nas_mmu_map_page(va, true)) fail("cannot map user stack");
    }
    uint64_t stack_pointer;
    for (unsigned fd = 0; fd < 3; fd++) {
        descriptors[fd] = FD_UART;
        file_slot[fd] = (uint8_t)fd;
        file_refcount[fd] = 1;
        file_status_flags[fd] = fd == 0 ? 0 : 1; /* stdin R, stdout/err W */
    }
    if (!nas_build_linux_stack(running_bash, running_busybox, &stack_pointer))
        fail("cannot initialize Linux userspace stack");
    nas_print("[ELF] userspace entry="); nas_hex(entry); nas_print("\n");
    if (running_bash) nas_print("[M2] entering real Bash in ring 3\n");
    else if (running_alpine) nas_print("[M4] real Alpine busybox via musl interpreter in ring 3\n");
    else if (running_busybox) nas_print("[M2] entering real BusyBox in ring 3\n");
    else nas_print("[M1] entering actual ring 3\n");
    nas_arch_enter_user(entry, stack_pointer);
    fail("iretq returned");
}
