#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Real Linux-ABI PID1 user-mode supervisor. The kernel knows nothing
 * about BusyBox or the service. --naseed has finite epochs for QEMU CI. */
static void die(const char *why) {
    fprintf(stderr, "m13 init error: %s errno=%d\n", why, errno);
    exit(1);
}
static void wait_exact(pid_t child) {
    int status=-1;
    if (waitpid(child,&status,0)!=child || !WIFEXITED(status) ||
        WEXITSTATUS(status)!=0) die("supervised child exit or reap");
}
static pid_t launch(const char *path,const char *arg0,const char *arg1,
                    const char *arg2) {
    pid_t child=fork();
    if (child<0) die("fork service");
    if (!child) {
        char *const args[]={(char *)arg0,(char *)arg1,(char *)arg2,NULL};
        execv(path,args);
        _exit(127);
    }
    return child;
}
static void reap_orphan(pid_t service) {
    int status=-1;
    pid_t orphan=waitpid(-1,&status,0);
    if (orphan<=1 || orphan==service || !WIFEXITED(status) ||
        WEXITSTATUS(status)!=0) die("adopted grandchild exit/reap");
    errno=0;
    if (waitpid(-1,&status,WNOHANG)!=-1 || errno!=ECHILD)
        die("unreaped descendants after init adoption");
}
static int linux_oracle(const char *self) {
    for (int round=0;round<3;++round)
        wait_exact(launch(self,self,"--helper",NULL));
    puts("m13-init-ok");
    return 0;
}
int main(int argc,char **argv) {
    if (argc==2 && !strcmp(argv[1],"--helper")) return 0;
    if (argc==2 && !strcmp(argv[1],"--linux-oracle"))
        return linux_oracle(argv[0]);
    if (argc!=2 || strcmp(argv[1],"--naseed") || getpid()!=1)
        die("expected real NaSeed PID1 boot");
    for (int round=0;round<3;++round) {
        pid_t service=launch("/bin/m12-process","m12-process",
                             "--naseed-limit",NULL);
        wait_exact(service);
        reap_orphan(service);
        wait_exact(launch("/bin/busybox","busybox","cat","/etc/hostname"));
        puts("m13-init-round-ok");
    }
    puts("m13-init-ok");
    return 0;
}
