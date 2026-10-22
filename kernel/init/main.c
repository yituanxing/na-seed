#include <stdint.h>
#include "../arch/abi.h"
#include <stdbool.h>
#include "../task.h"
#include "../fd.h"
#include "../boot_image.h"
#include "../arch/arch.h"
#include "../arch/console.h"
#include "../arch/mmu.h"
#include "../drivers/serial.h"
#include "../drivers/tty.h"
#include "../panic.h"
#include "../task/elf.h"
#include "../task/user_stack.h"
#include "../task/signal.h"
#include "../time/clock.h"
#include "main.h"
#include "../fs/file_ops.h"
#define TASK (*nas_current_task)
#define descriptors TASK.fds.descriptors
#define file_slot TASK.fds.file_slot
#define running_bash TASK.running_bash
#define running_busybox TASK.running_busybox
#define running_alpine TASK.running_alpine
#define USER_STACK_TOP NAS_USER_STACK_TOP
#define USER_STACK_PAGES 32

void nas_init_userspace(void) {
    nas_print("[INIT] creating common task/VM and architecture user entry\n");
    nas_task_boot_init();
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
        nas_fail("invalid or unmappable userspace ELF");
    for (uint64_t va = USER_STACK_TOP - USER_STACK_PAGES * PAGE;
         va < USER_STACK_TOP; va += PAGE) {
        if (!nas_mmu_map_page(va, true)) nas_fail("cannot map user stack");
    }
    if(!nas_signal_install_trampoline())
        nas_fail("cannot install native RX rt_sigreturn trampoline");
    uint64_t stack_pointer;
    for (unsigned fd = 0; fd < 3; fd++) {
        descriptors[fd] = FD_UART;
        file_slot[fd] = (uint8_t)fd;
        nas_open_files[fd].refcount = 1;
        nas_open_files[fd].ops = nas_fs_ops_for_kind(FD_UART);
        nas_open_files[fd].status_flags = fd == 0 ? 0 : 1; /* stdin R, stdout/err W */
    }
    if (!nas_build_linux_stack(running_bash, running_busybox, &stack_pointer))
        nas_fail("cannot initialize Linux userspace stack");
    nas_print("[ELF] userspace entry="); nas_hex(entry); nas_print("\n");
    if (running_bash) nas_print("[M2] entering real Bash in ring 3\n");
    else if (running_alpine) nas_print("[M4] real Alpine busybox via musl interpreter in ring 3\n");
    else if (running_busybox) nas_print("[M2] entering real BusyBox in ring 3\n");
    else nas_print("[M1] entering actual ring 3\n");
    nas_arch_enter_user(entry, stack_pointer);
    nas_fail("architecture user-entry unexpectedly returned");
}
