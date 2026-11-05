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
void nas_init_process_deferred_exit_service(void);

static struct nas_task init_task;
struct nas_task *nas_current_task=&init_task;
static jmp_buf successful_debug_exit;
static bool armed, completed;
static unsigned quiesced, requested, unmounted, exited;

void nas_print(const char *s) {(void)s;}
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
bool nas_lwext4_guest_boot_shutdown(void) {
    assert(quiesced==1 && unmounted==0);
    assert(!armed || (requested==1 && completed));
    unmounted++;
    return true;
}
void nas_platform_debug_exit(unsigned result) {
    assert(result==0 && unmounted==1 && quiesced==1);
    exited++;
    longjmp(successful_debug_exit,1);
}

int main(int argc,char **argv) {
    assert(argc==2);
    assert(strcmp(argv[1],"--armed")==0 ||
           strcmp(argv[1],"--unarmed")==0);
    armed=strcmp(argv[1],"--armed")==0;
    init_task.pid=init_task.tgid=1;
    init_task.state=NAS_TASK_ZOMBIE;
    nas_init_process_terminated(0,0);
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
    puts(armed ? "M191_PID1_M180_ORDER_PASS" :
                 "M191_PID1_UNARMED_ORDER_PASS");
    return 0;
}
