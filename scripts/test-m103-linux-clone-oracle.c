/* Actual Linux x86-64 syscall oracle, not a simulated NaSeed stub.
 * CHILD_SETTID is visible inside a newly created process's private MM,
 * not the parent's. Bad CLONE_*SETTID pointers are ignored after clone;
 * bad clear_child_tid must not kill the machine during SYS_exit. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#if !defined(__linux__) || !defined(__x86_64__)
int main(void) {
    puts("[M103 LINUX ORACLE SKIP] requires Linux x86-64 raw clone ABI");
    return 0;
}
#else
static int clone_process(unsigned long flags,int *parent_tid,int *child_tid,
                          bool check_child_set) {
    long pid=syscall(SYS_clone,flags,0,parent_tid,child_tid,0);
    if(pid==0) {
        if(check_child_set && *child_tid!=(int)syscall(SYS_getpid))
            syscall(SYS_exit,71);
        if(syscall(SYS_set_tid_address,(int *)(uintptr_t)1)!=syscall(SYS_getpid))
            syscall(SYS_exit,72);
        syscall(SYS_exit,0);
        __builtin_unreachable();
    }
    assert(pid>0);
    int status=0;
    assert(waitpid((pid_t)pid,&status,0)==pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status)==0);
    return (int)pid;
}
int main(void) {
    int *tid=mmap(0,4096,PROT_READ|PROT_WRITE,
                  MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(tid!=MAP_FAILED);
    *tid=0x3a29;
    /* Linux schedule_tail writes CHILD_SETTID after child MM switch.
     * Child sees its own PID, parent retains its original COW data. */
    clone_process(CLONE_CHILD_SETTID|CLONE_CHILD_CLEARTID|SIGCHLD,
                  0,tid,true);
    assert(*tid==0x3a29);
    /* Invalid child and parent pointers cannot turn an existing child
     * into an invisible zombie or cause an EFAULT return from clone. */
    clone_process(CLONE_CHILD_SETTID|CLONE_CHILD_CLEARTID|SIGCHLD,
                  0,(int *)(uintptr_t)1,false);
    clone_process(CLONE_PARENT_SETTID|SIGCHLD,
                  (int *)(uintptr_t)1,0,false);
    assert(munmap(tid,4096)==0);
    puts("[M103 LINUX ORACLE PASS] real clone CHILD_SETTID/COW, bad parent+child TID, invalid clear_child_tid and waitpid");
    return 0;
}
#endif
