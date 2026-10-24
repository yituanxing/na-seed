/* Compiled by genuine distro GCC INSIDE NaSeed; no test-only ABI.
 * 24 live fork children blocked on one shared pipe simultaneously.
 * This exceeds the previous 16-slot Task registry by a wide margin.
 * Parent verifies each PID via waitpid after admitting ALL 24.
 * Distinct write/read bytes enforce actual pipe/unblock and reaping.
 */
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

enum { CHILDREN=24 };
static void fail(const char *s) {
    perror(s);
    _exit(83);
}
int main(void) {
    int p[2];
    pid_t tids[CHILDREN];
    if(pipe(p)!=0)fail("pipe");
    for(int i=0;i<CHILDREN;i++) {
        pid_t pid=fork();
        if(pid<0)fail("24 simultaneous fork calls");
        if(pid==0) {
            char b=0;
            close(p[1]);
            ssize_t n=read(p[0],&b,1);
            if(n!=1 || b!='K')_exit(90);
            close(p[0]);
            _exit(0);
        }
        tids[i]=pid;
    }
    /* Every child is live at this point: none has received a byte.
     * The old NAS_TASK_LIMIT=16 implementation returns EAGAIN above. */
    printf("M49_24_SIMULTANEOUS_CHILDREN_PASS\n");
    if(close(p[0]))fail("close shared parent read");
    for(int i=0;i<CHILDREN;i++)
        if(write(p[1],"K",1)!=1)fail("write wake byte");
    if(close(p[1]))fail("close parent writer");
    for(int i=0;i<CHILDREN;i++) {
        int status=-1;
        if(waitpid(tids[i],&status,0)!=tids[i] ||
           !WIFEXITED(status) || WEXITSTATUS(status)!=0)
            fail("waitpid/reap each child");
    }
    puts("M49_24_CHILDREN_WAIT4_REAP_PASS");
    return 0;
}
