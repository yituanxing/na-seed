#include <stdint.h>
#include "../arch/abi.h"
#include <stdbool.h>
#include "../task.h"
#include "../fd.h"
#include "../boot_image.h"
#include "../syscall/reboot_policy.h"
#include "../errno.h"
#include "../lib/string.h"
#if defined(NAS_ENABLE_M191_ALPINE_ROOT) || \
    defined(NAS_ENABLE_H8_QEMU_ALPINE_ROOT) || \
    defined(NAS_ENABLE_H9_QEMU_AHCI_USERLAND)
#define NAS_ENABLE_EXTERNAL_EXT4_BOOT_ROOT 1
#endif
#ifdef NAS_ENABLE_H7_QEMU_NVME_USERLAND
#include "../platform/x86_h3_nvme.h"
#endif
#ifdef NAS_ENABLE_H9_QEMU_AHCI_USERLAND
#include "../platform/x86_h9_ahci.h"
#endif
#ifdef NAS_ENABLE_H10_QEMU_E1000_USERLAND
#include "../platform/x86_h10_e1000.h"
#endif
#ifdef NAS_ENABLE_LWEXT4
extern bool nas_lwext4_guest_boot_mount(void);
extern bool nas_lwext4_guest_boot_shutdown(void);
#endif
#ifdef NAS_ENABLE_M179_FS_IRQ
extern bool nas_lwext4_async_runtime_passed(void);
extern bool nas_lwext4_async_runtime_armed(void);
extern void nas_lwext4_async_runtime_step(void);
#endif
#if defined(NAS_ENABLE_M179_FS_IRQ) || defined(NAS_ENABLE_EXTERNAL_EXT4_BOOT_ROOT)
void nas_init_process_deferred_exit_service(void);
static bool pid1_exit_waiting;
#endif
#ifdef NAS_ENABLE_EXTERNAL_EXT4_BOOT_ROOT
static bool pid1_shutdown_quiesced;
static uint64_t pid1_shutdown_signals;
static uint32_t pid1_power_command;
#ifdef NAS_ENABLE_H10_QEMU_E1000_USERLAND
static bool h10_nic_shutdown_done;
#endif
#ifdef NAS_ENABLE_M257_DEBIAN_SHUTDOWN
static bool pid1_debian_power;
#endif
#endif
#ifdef NAS_ENABLE_M180_FS_USER_IRQ
extern void nas_lwext4_m180_shutdown_request(void);
extern bool nas_lwext4_m180_shutdown_complete(void);
#endif
#ifdef NAS_ENABLE_LITTLEFS
extern bool nas_littlefs_guest_boot_mount(void);
extern bool nas_littlefs_guest_boot_shutdown(void);
#endif
#ifdef NAS_ENABLE_RAMFS
extern bool nas_ramfs_guest_boot_mount(void);
extern bool nas_ramfs_guest_boot_shutdown(void);
#endif
extern bool nas_lwip_guest_boot(void);
#include "../arch/arch.h"
#include "../arch/console.h"
#include "../arch/mmu.h"
#include "../drivers/serial.h"
#include "../drivers/tty.h"
#include "../drivers/pty.h"
#include "../drivers/core/misc.h"
#include "../drivers/display/fbdev.h"
#include "../panic.h"
#include "../task/elf.h"
#include "../task/user_stack.h"
#include "../task/signal.h"
#include "../task/kthread.h"
#include "../task/scheduler_service.h"
#include "../time/clock.h"
#include "main.h"
#include "../fs/file_ops.h"
#include "../platform/platform.h"
#include "../drivers/display/display.h"
#ifdef NAS_ENABLE_VIRTIO_BLK_ASYNC
#include "../../ports/uboot/async_blk.h"
#endif
#ifdef NAS_ENABLE_VIRTIO_GPU
#include "../drivers/display/virtio_gpu.h"
#endif
#ifdef NAS_ENABLE_VIRTIO_INPUT
#include "../drivers/input/virtio_input.h"
#endif
#define TASK (*nas_current_task)

/* Boot owns the ordered service composition. The scheduler never names an
 * individual driver. Every callback checks its own live device state. */
#ifdef NAS_ENABLE_VIRTIO_BLK_ASYNC
static void deferred_async_block(void *context) {
    (void)context;
    nas_async_blk_service();
    nas_async_blk_native_test_step();
}
#endif
#ifdef NAS_ENABLE_M179_FS_IRQ
static void deferred_lwext4_worker(void *context) {
    (void)context;
    nas_lwext4_async_runtime_step();
}
#endif
#if defined(NAS_ENABLE_M179_FS_IRQ) || defined(NAS_ENABLE_EXTERNAL_EXT4_BOOT_ROOT)
static void deferred_pid1_exit(void *context) {
    (void)context;
    nas_init_process_deferred_exit_service();
}
#endif
#ifdef NAS_ENABLE_VIRTIO_GPU
static void deferred_gpu_scanout(void *context) {
    (void)context;
    if(nas_display_primary() && nas_display_present()<0)
        nas_fail("GPU scanout update failed");
    nas_virtio_gpu_report();
}
#endif
#ifdef NAS_ENABLE_VIRTIO_INPUT
static void deferred_input_events(void *context) {
    (void)context;
    (void)nas_virtio_input_poll(64U);
    nas_virtio_input_irq_report();
}
#endif
static void init_deferred_services(void) {
    int rc=0;
#ifdef NAS_ENABLE_VIRTIO_BLK_ASYNC
    rc=nas_sched_register_deferred(NAS_SCHED_BEFORE_WORKERS,
                                   deferred_async_block,NULL);
    if(rc)nas_fail("deferred block service registration failed");
#endif
#ifdef NAS_ENABLE_M179_FS_IRQ
    rc=nas_sched_register_deferred(NAS_SCHED_BEFORE_WORKERS,
                                   deferred_lwext4_worker,NULL);
    if(rc)nas_fail("deferred ext4 service registration failed");
#endif
#ifdef NAS_ENABLE_VIRTIO_GPU
    rc=nas_sched_register_deferred(NAS_SCHED_BEFORE_WORKERS,
                                   deferred_gpu_scanout,NULL);
    if(rc)nas_fail("deferred GPU service registration failed");
#endif
#ifdef NAS_ENABLE_VIRTIO_INPUT
    rc=nas_sched_register_deferred(NAS_SCHED_BEFORE_WORKERS,
                                   deferred_input_events,NULL);
    if(rc)nas_fail("deferred input service registration failed");
#endif
#if defined(NAS_ENABLE_M179_FS_IRQ) || defined(NAS_ENABLE_EXTERNAL_EXT4_BOOT_ROOT)
    /* PID1 success requires the real filesystem worker's durable shutdown. */
    rc=nas_sched_register_deferred(NAS_SCHED_AFTER_WORKERS,
                                   deferred_pid1_exit,NULL);
    if(rc)nas_fail("deferred PID1 exit service registration failed");
#endif
    (void)rc;
}

/* Only a FULLY terminated PID 1 process may drive the host test exit.
 * The scheduler publishes its group-finalization event; the boot policy
 * stays here, never in an ISA trap or the syscall dispatch hot path. */
static void nas_init_finish_success(void) {
#ifdef NAS_ENABLE_EXTERNAL_EXT4_BOOT_ROOT
    /* The physical ext4 filesystem is /, and all other mounts attach to
     * its Dentry tree. Detach any additional guests before the root. */
#ifdef NAS_ENABLE_RAMFS
    if (!nas_ramfs_guest_boot_shutdown())
        nas_fail("upstream ramfs guest VFS unmount failed");
#endif
#ifdef NAS_ENABLE_LITTLEFS
    if (!nas_littlefs_guest_boot_shutdown())
        nas_fail("littlefs guest VFS unmount failed");
#endif
#endif
#ifdef NAS_ENABLE_LWEXT4
    /* A successful fsync is NOT a clean filesystem shutdown. Keep the
     * ext4 mount and BlockDevice alive until PID1's final Files have
     * been closed, then run upstream ext4_umount and durable disk flush.
     * Applies to both RAM and hardware devices, no ISA branches. */
    if (!nas_lwext4_guest_boot_shutdown())
        nas_fail("lwext4 final unmount/durable device flush failed");
#endif
#ifdef NAS_ENABLE_H7_QEMU_NVME_USERLAND
    /* The genuine NVMe-backed ELF and data Files have closed, and lwext4
     * dropped its BlockDevice lease. Only now may the QEMU-only session
     * disarm DMA, disable PCI bus mastering and release the BAR claim. A
     * failed quiesce must NEVER produce a userland PASS marker. */
    if (nas_x86_h7_nvme_shutdown())
        nas_fail("H7 NVMe controller shutdown or BlockDevice detach failed");
#ifdef NAS_ENABLE_H8_QEMU_ALPINE_ROOT
    nas_print("[H8 NVME ALPINE ROOT SHUTDOWN] ext4 unmounted; controller quiesced\n");
#endif
    nas_print("[H7 NVME USERLAND SHUTDOWN] ext4 unmounted; controller quiesced\n");
#endif
#ifdef NAS_ENABLE_H9_QEMU_AHCI_USERLAND
    if(nas_x86_h9_ahci_shutdown())
        nas_fail("H9 SATA port/DMA/BAR shutdown failed");
    nas_print("[H9 AHCI USERLAND SHUTDOWN] ext4 unmounted; SATA port quiesced\n");
#endif
#ifndef NAS_ENABLE_EXTERNAL_EXT4_BOOT_ROOT
#ifdef NAS_ENABLE_LITTLEFS
    if (!nas_littlefs_guest_boot_shutdown())
        nas_fail("littlefs guest VFS unmount failed");
#endif
#ifdef NAS_ENABLE_RAMFS
    if (!nas_ramfs_guest_boot_shutdown())
        nas_fail("upstream ramfs guest VFS unmount failed");
#endif
#endif
#ifdef NAS_ENABLE_EXTERNAL_EXT4_BOOT_ROOT
    /* A failed/unsupported final reboot(2) may still leave original PID1
     * in its shutdown sleep. Always finish the real ext4
     * teardown BEFORE reporting this failure: panic-on-exit would otherwise
     * strand dirty filesystem buffers and invalidate subsequent boots. */
    if(pid1_shutdown_signals && !pid1_power_command)
        nas_fail("PID1 shutdown lacked a supported final reboot(2)");
#ifdef NAS_M241_PLATFORM_POWERDOWN
    /* Native platform powerdown requires the completed original BusyBox
     * or M257 signed Debian halt poweroff(2) contract. A generic PID1 exit
     * is never sufficient authority for SBI SRST / PSCI SYSTEM_OFF. */
    if(pid1_power_command!=NAS_LINUX_REBOOT_POWER_OFF
#ifdef NAS_M242_PLATFORM_REBOOT
       && pid1_power_command!=NAS_LINUX_REBOOT_RESTART
#endif
       )
        nas_fail("platform powerdown without authorized original POWER_OFF");
#endif
#ifdef NAS_M242_PLATFORM_REBOOT
    if(pid1_power_command==NAS_LINUX_REBOOT_RESTART) {
        /* This is the real post-PID1, post-ext4-shutdown firmware reset.
         * The previous boot must not produce a generic test PASS marker. */
        nas_print("[ALPINE REBOOT] original BusyBox; durable ext4 unmount\n");
        nas_print("[M242 RESET] real platform reset\n");
        nas_platform_restart();
        nas_fail("M242 platform reset returned unexpectedly");
    }
#endif
    /* Printed only AFTER final Files close, the async worker drains, and
     * the actual ext4 filesystem is durably unmounted. A QEMU test exit
     * is NOT a physical FPGA board's power controller. */
#ifdef NAS_ENABLE_M257_DEBIAN_SHUTDOWN
    if(pid1_debian_power) {
        if(pid1_power_command!=NAS_LINUX_REBOOT_POWER_OFF)
            nas_fail("M257 Debian shutdown did not authorize POWER_OFF");
        nas_print("[M257 DEBIAN POWER_OFF] original SysVinit halt; durable ext4 unmount\n");
    } else
#endif
    if(pid1_power_command==NAS_LINUX_REBOOT_POWER_OFF)
        nas_print("[ALPINE POWER_OFF] original BusyBox; durable ext4 unmount\n");
    else if(pid1_power_command==NAS_LINUX_REBOOT_HALT)
        nas_print("[ALPINE HALT] original BusyBox; durable ext4 unmount\n");
#endif
    if(nas_boot_argv_enabled())
        nas_print("[PASS] cmdline userspace exit 0\n");
    else
        nas_print("[PASS] userspace exit 0\n");
    nas_platform_debug_exit(0);
    nas_fail("QEMU debug exit returned unexpectedly");
}

void nas_init_note_pid1_shutdown_consumed(unsigned signal) {
#ifdef NAS_ENABLE_M191_ALPINE_ROOT
    const struct nas_task *init=nas_task_by_pid(1);
    if(!init || init->tgid!=1 || init->state==NAS_TASK_ZOMBIE)return;
    if(signal==NAS_SIGUSR1 || signal==NAS_SIGUSR2 || signal==NAS_SIGTERM)
        pid1_shutdown_signals |= UINT64_C(1)<<(signal-1);
#else
    (void)signal;
#endif
}

uint64_t nas_init_alpine_power_request(const struct nas_task *caller,
                                       uint32_t command) {
#ifdef NAS_ENABLE_M191_ALPINE_ROOT
    /* Original BusyBox PID1 runs shutdown actions, then vforks a child
     * for reboot(2) and enters an endless sleep. A direct `poweroff -f` from a
     * random shell cannot silently succeed while init stays alive. */
    const struct nas_task *init=nas_task_by_pid(1);
    if(!caller || caller->ppid!=1 || caller->tgid==1 ||
       caller->vfork_parent_pid!=1 || !caller->vfork_shared_vm ||
       !init || init->tgid!=1 || init->state==NAS_TASK_ZOMBIE)
        return (uint64_t)-EPERM;
    unsigned required_signal=0;
    if(command==NAS_LINUX_REBOOT_POWER_OFF)required_signal=NAS_SIGUSR2;
    else if(command==NAS_LINUX_REBOOT_HALT)required_signal=NAS_SIGUSR1;
    else if(command==NAS_LINUX_REBOOT_RESTART)required_signal=NAS_SIGTERM;
    else return (uint64_t)-EOPNOTSUPP;
    if(!(pid1_shutdown_signals&(UINT64_C(1)<<(required_signal-1))))
        return (uint64_t)-EPERM;
#ifndef NAS_M242_PLATFORM_REBOOT
    /* Outside the dedicated M242 profile there is no firmware reset path;
     * never claim that test debug-exit is a real physical reboot. */
    if(command==NAS_LINUX_REBOOT_RESTART)return (uint64_t)-EOPNOTSUPP;
#endif
    if(pid1_power_command && pid1_power_command!=command)
        return (uint64_t)-EBUSY;
    pid1_power_command=command;
    return 0;
#else
    (void)caller;(void)command;
    return (uint64_t)-EOPNOTSUPP;
#endif
}

uint64_t nas_init_debian_power_request(const struct nas_task *caller,
                                       uint32_t command) {
#if defined(NAS_ENABLE_M257_DEBIAN_SHUTDOWN) && defined(NAS_ENABLE_M191_ALPINE_ROOT)
    /* The dedicated real-Debian physical-root image uses signed SysVinit
     * PID1. Only a root-owned, committed original /usr/sbin/halt ELF that
     * was executed directly by PID1 may request POWER_OFF. PR_SET_NAME,
     * argv[0] and a random shell's reboot(2) have no authority. Host seed
     * verification pins the exact signed Debian binary on physical ext4. */
    const struct nas_task *init=nas_task_by_pid(1);
    if(command!=NAS_LINUX_REBOOT_POWER_OFF) {
        nas_print("[M257 HALT FAIL] original halt requested non-POWER_OFF command\n");
        return (uint64_t)-EOPNOTSUPP;
    }
    if(!caller || !caller->m257_original_debian_halt ||
       caller->pid==1 || caller->tgid==1 || caller->ppid!=1 ||
       caller->credentials.ruid!=0 || caller->credentials.euid!=0 ||
       caller->credentials.fsuid!=0 ||
       !init || init->pid!=1 || init->tgid!=1 ||
       init->state==NAS_TASK_ZOMBIE || init->group_exit_pending) {
        nas_print("[M257 HALT FAIL] rejected caller without signed PID1 child ELF origin\n");
        return (uint64_t)-EPERM;
    }
    if(pid1_power_command) {
        nas_print("[M257 HALT FAIL] duplicate power request\n");
        return (uint64_t)-EBUSY;
    }
    pid1_power_command=command;
    pid1_debian_power=true;
    nas_print("[M257 DEBIAN HALT AUTHORIZED] original signed ext4 halt requested POWER_OFF\n");
    return 0;
#else
    (void)caller;(void)command;
    return (uint64_t)-EOPNOTSUPP;
#endif
}

void nas_init_process_terminated(uint32_t code,uint32_t signal) {
    if(!nas_current_task || nas_current_task->pid!=1 ||
       nas_current_task->state!=NAS_TASK_ZOMBIE ||
       nas_current_task->group_exit_pending)
        nas_fail("PID1 test completion before final thread exit");
    if(signal)nas_fail("init terminated by default fatal signal");
    nas_print("[EXIT] user status=");nas_dec(code);nas_print("\n");
    if(code)nas_fail("userspace exited nonzero");
#ifdef NAS_ENABLE_EXTERNAL_EXT4_BOOT_ROOT
    /* A detached watchdog, daemon or worker may outlive a successful PID1.
     * Do not unmount its /dev or ext4 beneath real live OpenFiles. The
     * final scheduler boundary has its own stack and can quiesce all user
     * thread groups before any M180 worker shutdown request or unmount. */
    pid1_exit_waiting=true;
    return;
#else
#ifdef NAS_ENABLE_M179_FS_IRQ
    /* Compilation enables the M179/M180 worker, but a later independent
     * desktop boot of this SAME kernel may have no async disk fixture.
     * Only an actually armed worker owns the durable-shutdown exit gate.
     * Once armed, the existing strict M179/M180 completion checks apply. */
    if(!nas_lwext4_async_runtime_armed()) {
        nas_init_finish_success();
        return;
    }
    /* PID1 may finish before the disk IRQ-backed filesystem worker. Leave
     * its Task zombie and return to the independent native scheduler;
     * only a completed, flushed and unmounted real ext4 volume permits the
     * test-success debug exit. Never spin or pump a nested scheduler from
     * this dead Task's syscall stack. */
#ifdef NAS_ENABLE_M180_FS_USER_IRQ
    /* M180's live user mount must finish its worker-owned ext4_umount
     * AFTER the final PID1 Files release. The shutdown command is queued
     * here, never run recursively on the dying Task's kernel stack. */
    nas_lwext4_m180_shutdown_request();
    pid1_exit_waiting=true;
    return;
#else
    if(!nas_lwext4_async_runtime_passed()) {
        pid1_exit_waiting=true;
        return;
    }
#endif
#endif
    nas_init_finish_success();
#endif
}

#if defined(NAS_ENABLE_M179_FS_IRQ) || defined(NAS_ENABLE_EXTERNAL_EXT4_BOOT_ROOT)
void nas_init_process_deferred_exit_service(void) {
#ifdef NAS_ENABLE_EXTERNAL_EXT4_BOOT_ROOT
    if(pid1_power_command && !pid1_exit_waiting) {
        /* The real BusyBox shutdown parent never calls exit(2). Its legal
         * reboot vfork child must have fully exited and released the shared
         * VM before this scheduler-stack transition can finalize PID1. */
        const struct nas_task *init=nas_task_by_pid(1);
        if(!init)nas_fail("original PID1 missing at shutdown");
        if(init->state!=NAS_TASK_ZOMBIE && !init->vfork_child_pid) {
            if(nas_signal_shutdown_init())
                nas_fail("original Alpine scheduler PID1 exit failed");
        }
    }
    if(!pid1_exit_waiting)return;
    if(!pid1_shutdown_quiesced) {
        /* Bound to one Task-table scan and the normal SIGKILL exit_group
         * implementation. No wait4, nested task_schedule, or EBUSY bypass. */
        if(nas_signal_shutdown_users())
            nas_fail("PID1 deferred user Task quiescence failed");
        pid1_shutdown_quiesced=true;
        nas_print("[M191 PID1 QUIESCED] no live non-init user Tasks\n");
#if defined(NAS_ENABLE_M179_FS_IRQ) && defined(NAS_ENABLE_M180_FS_USER_IRQ)
        /* M180 must not start durable teardown while orphaned user FDs
         * still pin pseudo mounts. Queue it only after quiescence. */
        if(nas_lwext4_async_runtime_armed())
            nas_lwext4_m180_shutdown_request();
#endif
    }
#ifdef NAS_ENABLE_M179_FS_IRQ
    if(nas_lwext4_async_runtime_armed()) {
#ifdef NAS_ENABLE_M180_FS_USER_IRQ
        if(!nas_lwext4_m180_shutdown_complete())return;
#else
        if(!nas_lwext4_async_runtime_passed())return;
#endif
    }
#endif
#ifdef NAS_ENABLE_H10_QEMU_E1000_USERLAND
    /* The lwIP core thread may still hold a pinned e10000 NetDevice even
     * after PID1 and its descriptors have exited. Device removal notifies
     * the existing network worker; defer actual PCI DMA/BAR release until
     * that worker has asynchronously detached the netif. No nested task
     * scheduling or busy-spin is allowed from the terminating task. */
    if(!h10_nic_shutdown_done) {
        int nic_rc=nas_x86_h10_e1000_shutdown();
        if(nic_rc==-EBUSY)return;
        if(nic_rc)nas_fail("H10 E1000 worker detach or DMA shutdown failed");
        h10_nic_shutdown_done=true;
    }
#endif
#else
#ifdef NAS_ENABLE_M180_FS_USER_IRQ
    if(!pid1_exit_waiting || !nas_lwext4_m180_shutdown_complete())return;
#else
    if(!pid1_exit_waiting || !nas_lwext4_async_runtime_passed())return;
#endif
    /* Called by the post-worker service after a bounded slice. All PID1
     * Files/cwd pins were released by the normal process-exit path. */
#endif
    pid1_exit_waiting=false;
    nas_init_finish_success();
}
#endif

void nas_init_userspace(void) {
    nas_print("[INIT] creating common task/VM and architecture user entry\n");
    nas_task_boot_init();
    /* Character class publication is independent of a mounted /dev.
     * The VFS namei path still wins whenever a real inode exists. */
    if(nas_misc_chrdev_init() || nas_tty_chrdev_init() ||
       nas_pty_chrdev_init() || nas_fbdev_chrdev_init())
        nas_fail("base character-device publication failed");
#ifdef NAS_ENABLE_VIRTIO_INPUT
    /* Publish the event character-device handler before the shared bus
     * can discover a VirtIO input device (through lwIP or block boot). */
    extern int nas_input_chrdev_init(void);
    if(nas_input_chrdev_init())
        nas_fail("evdev character-device registration failed");
#endif
    /* Native QEMU hardware acceptance: exercise real kernel C contexts
     * before user syscalls begin using the same continuation mechanism. */
    nas_kernel_context_boot_selftest();
    nas_kthread_system_init();
    nas_kthread_boot_selftest();
    init_deferred_services();
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
#ifdef NAS_ENABLE_EXTERNAL_EXT4_BOOT_ROOT
    /* Keep CPIO as an immutable rescue archive, but take PID1 from the
     * official Alpine ext4 root after its real BlockDevice is registered.
     * Select root ownership before any pseudo mount gets a parent Dentry. */
    if(!nas_boot_argv_first())
        nas_fail("external ext4 root requires explicit naseed.argv=/absolute/elf");
#ifdef NAS_ENABLE_M257_DEBIAN_SHUTDOWN
    if(strcmp(nas_boot_argv_first(),"/sbin/init")!=0)
        nas_fail("M257 requires original Debian /sbin/init PID1 from physical ext4");
#endif
    const char *boot_error=nas_boot_prepare(NULL,&selected_image);
    if(nas_vfs_prepare_external_boot_root())
        nas_fail("external root selection happened after VFS registration");
#else
    const char *boot_error = nas_boot_prepare(
        nas_boot_argv_first(), &selected_image);
#endif
    if (boot_error) nas_fail(boot_error);
#ifdef NAS_ENABLE_LWEXT4
    /* After the archive is indexed and before the actual userspace ELF
     * starts, attach any separately loaded on-disk ext4 image. No syscall
     * or architecture-specific mount dispatch is introduced here. */
    if (!nas_lwext4_guest_boot_mount())
        nas_fail("opt-in lwext4 Limine module/VFS mount failed");
#endif
#ifdef NAS_ENABLE_EXTERNAL_EXT4_BOOT_ROOT
    boot_error=nas_boot_prepare_vfs_root(nas_boot_argv_first(),
                                         &selected_image);
    if(boot_error)nas_fail(boot_error);
#endif
#ifdef NAS_ENABLE_LITTLEFS
    if (!nas_littlefs_guest_boot_mount())
        nas_fail("opt-in littlefs guest VFS mount failed");
#endif
#ifdef NAS_ENABLE_RAMFS
    if (!nas_ramfs_guest_boot_mount())
        nas_fail("opt-in independent upstream ramfs VFS mount failed");
#endif
    /* The native tcpip worker is independent of filesystems and AF_UNIX.
     * It uses the same kernel continuation scheduler as every other worker. */
    if (!nas_lwip_guest_boot())
        nas_fail("upstream lwIP native bootstrap failed");
    struct elf_image main_image,interp_image;
    nas_boot_image_elf(&selected_image,&main_image,&interp_image);
    const struct elf_image *interpreter =
        selected_image.has_interpreter ? &interp_image : 0;
    uint64_t entry = 0;
    if (!nas_elf_load(&main_image, interpreter,
                      selected_image.interpreter_path[0] ?
                      selected_image.interpreter_path : NULL, &entry))
        nas_fail("invalid or unmappable userspace ELF");
    nas_boot_image_release(&selected_image);
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
    /* One bootstrap bootfb -> GPU handoff is possible while assembling the
     * machine, never after a process can open /dev/fb0. */
    nas_display_lock_primary();
    nas_arch_enter_user(entry, stack_pointer);
    nas_fail("architecture user-entry unexpectedly returned");
}
