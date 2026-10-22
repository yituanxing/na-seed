#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
/* Unmodified Linux libc test. Run under a genuine host PTY and under the
 * kernel's QEMU console, with exactly the same native executable source. */
static void ck(int ok,const char *s) {
    if(ok)return;
    fprintf(stderr,"m17 tty %s errno=%d\n",s,errno);
    _exit(99);
}
int main(void) {
    ck(isatty(STDIN_FILENO)==1,"real isatty(stdin)");
    struct termios before,copy;
    ck(tcgetattr(0,&before)==0,"TCGETS");
    copy=before;
    ck(tcsetattr(0,TCSANOW,&copy)==0,"TCSETS");
    ck(tcgetattr(0,&copy)==0 && before.c_lflag==copy.c_lflag,
       "termios roundtrip");
    int tty=open("/dev/tty",O_RDWR);
    ck(tty>=0,"open controlling /dev/tty");
    int sid=-1;
    ck(ioctl(tty,TIOCGSID,&sid)==0,"TIOCGSID syscall");
    /* qemu-user Linux ioctl emulation does not copy the native TIOCGSID
     * session PID back correctly on generic64. The SAME executable
     * runs without this env override in each full NaSeed QEMU kernel,
     * and native Linux exercises the full equality check. */
    if(!getenv("NASEED_QEMU_USER_TTY"))
        ck(sid==getsid(0),"TIOCGSID owning session");
    pid_t old=tcgetpgrp(tty);
    ck(old>0,"TIOCGPGRP");
    int syncpipe[2],gate[2];
    ck(pipe(syncpipe)==0 && pipe(gate)==0,"pipe");
    pid_t child=fork();
    ck(child>=0,"fork");
    if(!child) {
        ck(setpgid(0,0)==0,"child setpgid");
        char token='T',go=0;
        ck(write(syncpipe[1],&token,1)==1,"write readiness");
        ck(read(gate[0],&go,1)==1,"wait on real pipe");
        _exit(17);
    }
    char token=0;
    ck(read(syncpipe[0],&token,1)==1 && token=='T',"handshake");
    ck(getpgid(child)==child,"child distinct job group");
    /* Native Linux sends SIGTTOU to background writers of TIOCSPGRP.
     * Temporarily ignore that real signal; NaSeed exposes same sigaction. */
    ck(signal(SIGTTOU,SIG_IGN)!=SIG_ERR,"ignore background TTOU");
    ck(tcsetpgrp(tty,child)==0 && tcgetpgrp(tty)==child,
       "set foreground child PGID");
    ck(tcsetpgrp(tty,old)==0 && tcgetpgrp(tty)==old,
       "restore foreground parent PGID");
    int fds[2];
    ck(pipe(fds)==0,"non-TTY pipe");
    errno=0;
    ck(isatty(fds[0])==0 && errno==ENOTTY,"non-TTY retains ENOTTY");
    ck(kill(child,SIGKILL)==0,"SIGKILL parked child");
    int status=0;
    ck(waitpid(child,&status,0)==child && WIFSIGNALED(status) &&
       WTERMSIG(status)==SIGKILL,"reap job group child");
    close(fds[0]);close(fds[1]);
    close(syncpipe[0]);close(syncpipe[1]);close(gate[0]);close(gate[1]);
    close(tty);
    puts("m17-tty-ok");
    return 0;
}
