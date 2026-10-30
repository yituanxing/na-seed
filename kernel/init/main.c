#include <stdint.h>
#include "../arch/abi.h"
#include <stdbool.h>
#include "../task.h"
#include "../fd.h"
#include "../boot_image.h"
#ifdef NAS_ENABLE_LWEXT4
extern bool nas_lwext4_guest_boot_mount(void);
extern bool nas_lwext4_guest_boot_shutdown(void);
#endif
#ifdef NAS_ENABLE_LITTLEFS
extern bool nas_littlefs_guest_boot_mount(void);
extern bool nas_littlefs_guest_boot_shutdown(void);
#endif
#ifdef NAS_ENABLE_RAMFS
extern bool nas_ramfs_guest_boot_mount(void);
extern bool nas_ramfs_guest_boot_shutdown(void);
#endif
#include "../arch/arch.h"
#include "../arch/console.h"
#include "../arch/mmu.h"
#include "../drivers/serial.h"
#include "../drivers/tty.h"
#include "../panic.h"
#include "../task/elf.h"
#include "../task/user_stack.h"
#include "../task/signal.h"
#include "../task/kthread.h"
#include "../time/clock.h"
#include "main.h"
#include "../fs/file_ops.h"
#include "../platform/platform.h"
#define TASK (*nas_current_task)

/* Only a FULLY terminated PID 1 process may drive the host test exit.
 * The scheduler publishes its group-finalization event; the boot policy
 * stays here, never in an ISA trap or the syscall dispatch hot path. */
void nas_init_process_terminated(uint32_t code,uint32_t signal) {
    if(!nas_current_task || nas_current_task->pid!=1 ||
       nas_current_task->state!=NAS_TASK_ZOMBIE ||
       nas_current_task->group_exit_pending)
        nas_fail("PID1 test completion before final thread exit");
    if(signal)nas_fail("init terminated by default fatal signal");
    nas_print("[EXIT] user status=");nas_dec(code);nas_print("\n");
    if(code)nas_fail("userspace exited nonzero");
#ifdef NAS_ENABLE_LWEXT4
    /* A successful fsync is NOT a clean filesystem shutdown. Keep the
     * ext4 mount and BlockDevice alive until PID1's final Files have
     * been closed, then run upstream ext4_umount and durable disk flush.
     * Applies to both RAM and hardware devices, no ISA branches. */
    if (!nas_lwext4_guest_boot_shutdown())
        nas_fail("lwext4 final unmount/durable device flush failed");
#endif
#ifdef NAS_ENABLE_LITTLEFS
    if (!nas_littlefs_guest_boot_shutdown())
        nas_fail("littlefs guest VFS unmount failed");
#endif
#ifdef NAS_ENABLE_RAMFS
    if (!nas_ramfs_guest_boot_shutdown())
        nas_fail("upstream ramfs guest VFS unmount failed");
#endif
    if(nas_boot_argv_enabled())
        nas_print("[PASS] cmdline userspace exit 0\n");
    else
        nas_print("[PASS] userspace exit 0\n");
    nas_platform_debug_exit(0);
    nas_fail("QEMU debug exit returned unexpectedly");
}

void nas_init_userspace(void) {
    nas_print("[INIT] creating common task/VM and architecture user entry\n");
    nas_task_boot_init();
    /* Native QEMU hardware acceptance: exercise real kernel C contexts
     * before user syscalls begin using the same continuation mechanism. */
    nas_kernel_context_boot_selftest();
    nas_kthread_system_init();
    nas_kthread_boot_selftest();
    /* User console must be a hardware/firmware input backend BEFORE
     * TIOCSCTTY enables terminal RX. x86 COM1 and RV SBI work early;
     * ARM maps its real PL011 Device page into TTBR1 only after frame init. */
    if (!nas_arch_console_enable_input())
        nas_fail("native console physical RX/MMIO initialization failed");
    nas_tty_init(); /* SID/PGID from real PID1 */
    nas_arch_prepare_user_cpu();
    /* Hardware clocksource is now active; correlate boot RTC epoch once.
     * The timekeeper is shared across all native Linux ISA backends. */
    extern int64_t nas_boot_date_at_boot(void);
    nas_timekeeper_init(nas_boot_date_at_boot());
    if (!nas_mmu_bootstrap()) nas_fail("architecture user VM bootstrap failed");
    nas_arch_enable_user_traps();
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
            nas_fail("invalid Limine naseed.argv configuration");
    }
    /* File-system bytes and the selected executable are separate:
     * boot_image.c recognizes the Limine handoff and indexes CPIO via
     * bootfs.c; only this layer maps the actual ELF into userspace.
     */
    struct nas_boot_image selected_image;
    const char *boot_error = nas_boot_prepare(
        nas_boot_argv_first(), &selected_image);
    if (boot_error) nas_fail(boot_error);
#ifdef NAS_ENABLE_LWEXT4
    /* After the archive is indexed and before the actual userspace ELF
     * starts, attach any separately loaded on-disk ext4 image. No syscall
     * or architecture-specific mount dispatch is introduced here. */
    if (!nas_lwext4_guest_boot_mount())
        nas_fail("opt-in lwext4 Limine module/VFS mount failed");
#endif
#ifdef NAS_ENABLE_LITTLEFS
    if (!nas_littlefs_guest_boot_mount())
        nas_fail("opt-in littlefs guest VFS mount failed");
#endif
#ifdef NAS_ENABLE_RAMFS
    if (!nas_ramfs_guest_boot_mount())
        nas_fail("opt-in independent upstream ramfs VFS mount failed");
#endif
    struct elf_image main_image = {
        .data = selected_image.image, .size = selected_image.size
    };
    struct elf_image interp_image = {
        .data = selected_image.interpreter, .size = selected_image.interpreter_size
    };
    const struct elf_image *interpreter =
        selected_image.has_interpreter ? &interp_image : 0;
    uint64_t entry = 0;
    if (!nas_elf_load(&main_image, interpreter, &entry))
        nas_fail("invalid or unmappable userspace ELF");
    if (!nas_user_stack_map_initial()) nas_fail("cannot map user stack");
    if(!nas_signal_install_trampoline())
        nas_fail("cannot install native RX rt_sigreturn trampoline");
    uint64_t stack_pointer;
    for (unsigned fd = 0; fd < 3; fd++) {
        if(!nas_fd_install_new(nas_current_task,fd,fd,FD_UART,
                               fd==0 ? 0 : 1,false))
            nas_fail("cannot install initial Linux console descriptor");
    }
    if (!nas_build_linux_stack(selected_image.bash, selected_image.busybox,
                               &stack_pointer))
        nas_fail("cannot initialize Linux userspace stack");
    nas_print("[ELF] userspace entry="); nas_hex(entry); nas_print("\n");
    if (selected_image.bash) nas_print("[M2] entering real Bash in ring 3\n");
    else if (selected_image.alpine) nas_print("[M4] real Alpine busybox via musl interpreter in ring 3\n");
    else if (selected_image.busybox) nas_print("[M2] entering real BusyBox in ring 3\n");
    else nas_print("[M1] entering actual ring 3\n");
    nas_arch_enter_user(entry, stack_pointer);
    nas_fail("architecture user-entry unexpectedly returned");
}
