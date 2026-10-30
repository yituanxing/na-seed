/* Link against the unchanged target libc in the real x86/RV64/ARM64
 * NaSeed guest and execute from its native QEMU shell. This is intentionally
 * a real userspace syscall/pthread load, distinct from the process.c host
 * fault-injection harness and the Linux x86 syscall oracle. */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile unsigned detached_started;
static void *bad_clear_tid_worker(void *unused) {
    (void)unused;
    long tid=syscall(SYS_gettid);
    if(tid<=0)return 0;
    /* This worker deliberately revokes the pthread runtime's normal
     * CLONE_CHILD_CLEARTID field, simulating an unmapped/invalid pointer
     * at thread-group exit. A kernel panic here would freeze the guest. */
    if(syscall(SYS_set_tid_address,(int *)(uintptr_t)1)!=tid)return 0;
    __atomic_store_n(&detached_started,1U,__ATOMIC_RELEASE);
    return 0;
}
static int wait_for_detached_start(void) {
    struct timespec delay={.tv_sec=0,.tv_nsec=10000000};
    for(unsigned tick=0;tick<200;tick++) {
        if(__atomic_load_n(&detached_started,__ATOMIC_ACQUIRE))return 0;
        (void)nanosleep(&delay,0);
    }
    return -1;
}
int main(void) {
    /* Every child has its own COW MM; set_tid_address accepts an invalid
     * user pointer immediately and must still allow normal exit/wait4. */
    for(unsigned n=0;n<8;n++) {
        pid_t child=fork();
        if(child<0)return 20;
        if(child==0) {
            long tid=syscall(SYS_gettid);
            if(syscall(SYS_set_tid_address,(int *)(uintptr_t)1)!=tid)
                _exit(21);
            _exit(0);
        }
        int status=0;
        if(waitpid(child,&status,0)!=child || !WIFEXITED(status) ||
           WEXITSTATUS(status))return 22;
    }
    /* Detached pthread finishes on a shared VM and exercises the exact
     * group cleanup path, without blocking pthread_join on the now-invalid
     * clear_child_tid field. */
    pthread_attr_t attr;
    if(pthread_attr_init(&attr))return 23;
    if(pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED))return 24;
    pthread_t thread;
    if(pthread_create(&thread,&attr,bad_clear_tid_worker,0))return 25;
    if(pthread_attr_destroy(&attr))return 26;
    if(wait_for_detached_start())return 27;
    /* Leave enough native scheduling opportunity for worker's SYS_exit
     * to run its invalid clear_child_tid handling before our final print. */
    struct timespec delay={.tv_sec=0,.tv_nsec=100000000};
    if(nanosleep(&delay,0))return 28;
    puts("[M103 GUEST PASS] fork/wait4 repeated + detached pthread invalid clear_child_tid without kernel panic");
    return 0;
}
