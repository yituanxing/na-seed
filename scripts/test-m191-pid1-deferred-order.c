/* Execute real kernel/init/main.c PID1 completion callbacks with mocks at
 * the SIGNAL and ext4 boundaries. A dead PID1 kernel stack must only queue
 * work; the scheduler callback must quiesce BEFORE M180 shutdown/unmount. */
#include <assert.h>
#include <stdbool.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/task.h"
#include "../kernel/init/main.h"
#include "../kernel/syscall/reboot_policy.h"
#include "../kernel/task/signal.h"
#include "../kernel/errno.h"
void nas_init_process_deferred_exit_service(void);

static struct nas_task init_task;
static struct nas_task busybox_vfork_child;
struct nas_task *nas_current_task=&init_task;
static jmp_buf successful_debug_exit;
static bool armed, completed;
static unsigned quiesced, requested, unmounted, exited;
static bool printed_poweroff, poweroff_request, running_busybox_init;

void nas_print(const char *s) {
    if(strstr(s,"[ALPINE POWER_OFF]"))printed_poweroff=true;
}
void nas_dec(uint64_t value) {(void)value;}
_Noreturn void nas_arch_halt(void) { abort(); }
bool nas_boot_argv_enabled(void) { return true; }
bool nas_lwext4_async_runtime_armed(void) { return armed; }
bool nas_lwext4_m180_shutdown_complete(void) { return completed; }
void nas_lwext4_m180_shutdown_request(void) {
    assert(armed && quiesced==1 && !requested && !unmounted);
    requested++;
}
int nas_signal_shutdown_users(void) {
    assert(init_task.state==NAS_TASK_ZOMBIE);
    assert(init_task.pid==1 && init_task.tgid==1);
    assert(!quiesced && !requested && !unmounted);
    quiesced++;
    return 0;
}
int nas_signal_shutdown_init(void) {
    /* Production does this on the scheduler stack AFTER reboot's vfork
     * child has been released. The original BusyBox parent never exits. */
    assert(running_busybox_init && poweroff_request);
    assert(init_task.state==NAS_TASK_BLOCKED);
    assert(init_task.vfork_child_pid==0);
    assert(!quiesced && !requested && !unmounted);
    init_task.state=NAS_TASK_ZOMBIE;
    nas_init_process_terminated(0,0);
    return 0;
}
bool nas_lwext4_guest_boot_shutdown(void) {
    assert(quiesced==1 && unmounted==0);
    assert(!armed || (requested==1 && completed));
    unmounted++;
    return true;
}
void nas_platform_debug_exit(unsigned result) {
    assert(result==0 && unmounted==1 && quiesced==1);
    assert(printed_poweroff==poweroff_request);
    exited++;
    longjmp(successful_debug_exit,1);
}
struct nas_task *nas_task_by_pid(uint64_t pid) {
    return pid==1 ? &init_task : NULL;
}

int main(int argc,char **argv) {
    assert(argc==2);
    assert(strcmp(argv[1],"--armed")==0 ||
           strcmp(argv[1],"--unarmed")==0 ||
           strcmp(argv[1],"--poweroff")==0 ||
           strcmp(argv[1],"--poweroff-armed")==0 ||
           strcmp(argv[1],"--poweroff-live")==0 ||
           strcmp(argv[1],"--poweroff-live-armed")==0);
    armed=strcmp(argv[1],"--armed")==0 ||
          strcmp(argv[1],"--poweroff-armed")==0 ||
          strcmp(argv[1],"--poweroff-live-armed")==0;
    poweroff_request=strncmp(argv[1],"--poweroff",10)==0;
    running_busybox_init=strncmp(argv[1],"--poweroff-live",15)==0;
    init_task.pid=init_task.tgid=1;
    busybox_vfork_child.pid=3;
    busybox_vfork_child.tgid=3;
    busybox_vfork_child.ppid=1;
    busybox_vfork_child.vfork_parent_pid=1;
    busybox_vfork_child.vfork_shared_vm=true;
    if(poweroff_request) {
        init_task.state=NAS_TASK_BLOCKED;
        assert(nas_init_alpine_power_request(&busybox_vfork_child,
                                             NAS_LINUX_REBOOT_POWER_OFF)
               ==(uint64_t)-EPERM); /* no preceding BusyBox signal */
        nas_init_note_pid1_shutdown_consumed(NAS_SIGUSR2);
        assert(nas_init_alpine_power_request(&init_task,
                                             NAS_LINUX_REBOOT_POWER_OFF)
               ==(uint64_t)-EPERM);
        busybox_vfork_child.vfork_parent_pid=2;
        assert(nas_init_alpine_power_request(&busybox_vfork_child,
                                             NAS_LINUX_REBOOT_POWER_OFF)
               ==(uint64_t)-EPERM);
        busybox_vfork_child.vfork_parent_pid=1;
        busybox_vfork_child.vfork_shared_vm=false;
        assert(nas_init_alpine_power_request(&busybox_vfork_child,
                                             NAS_LINUX_REBOOT_POWER_OFF)
               ==(uint64_t)-EPERM);
        busybox_vfork_child.vfork_shared_vm=true;
        assert(nas_init_alpine_power_request(&busybox_vfork_child,
                                             NAS_LINUX_REBOOT_RESTART)
               ==(uint64_t)-EPERM); /* not the consumed SIGUSR2 command */
        assert(nas_init_alpine_power_request(&busybox_vfork_child,
                                             NAS_LINUX_REBOOT_POWER_OFF)
               ==0);
        assert(!quiesced && !unmounted && !exited);
    }
    if(running_busybox_init) {
        /* No synthetic PID1 exit: its BusyBox shutdown wait is indefinite,
         * and only the legitimate child reboot(2) has been acknowledged. */
        init_task.state=NAS_TASK_BLOCKED;
    } else {
        init_task.state=NAS_TASK_ZOMBIE;
        nas_init_process_terminated(0,0);
    }
    /* Returning from PID1 exit without any unmount is the central guard. */
    assert(!quiesced && !requested && !unmounted && !exited);
    if(!setjmp(successful_debug_exit)) {
        nas_init_process_deferred_exit_service();
        if(armed) {
            assert(quiesced==1 && requested==1 && !unmounted && !exited);
            nas_init_process_deferred_exit_service();
            assert(quiesced==1 && requested==1 && !unmounted && !exited);
            completed=true;
            nas_init_process_deferred_exit_service();
        } else abort(); /* no worker: first deferred slice must finish */
    }
    assert(quiesced==1 && unmounted==1 && exited==1);
    assert(requested==(unsigned)armed);
    puts(poweroff_request ? "M191_ORIGINAL_ALPINE_POWER_OFF_DURABLE_PASS" :
         (armed ? "M191_PID1_M180_ORDER_PASS" :
                  "M191_PID1_UNARMED_ORDER_PASS"));
    return 0;
}
