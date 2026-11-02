#define _GNU_SOURCE
#include "protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

/* Independent terminal app: a real PTY, signed dialog --version through
 * BusyBox sh, keyboard input from compositor IPC and a live memfd surface. */
static void read_pty_until(int fd,const char *wanted,int version) {
    char collected[2048]={0};
    size_t used=0;
    while(used<sizeof(collected)-1) {
        struct pollfd ready={.fd=fd,.events=POLLIN};
        int poll_result=poll(&ready,1,30000);
        if(poll_result<0 && errno==EINTR)continue;
        if(poll_result<=0 || !(ready.revents&POLLIN)) {
            fprintf(stderr,"[M167 PTY] expected=%s bytes=%zu output=%.*s\n",
                    wanted,used,(int)used,collected);
            nasd_fail("PTY shell response timeout/disconnect");
        }
        ssize_t n=read(fd,collected+used,sizeof(collected)-1-used);
        if(n<0 && errno==EINTR)continue;
        nasd_require(n>0,"PTY shell returned EOF before expected output");
        used+=(size_t)n;collected[used]=0;
        if(strstr(collected,wanted)) {
            if(version && !strstr(collected,"Version")) {
                fprintf(stderr,"[M167 PTY] dialog output=%.*s\n",
                        (int)used,collected);
                nasd_fail("signed dialog version missing in actual PTY");
            }
            return;
        }
    }
    nasd_require(0,"PTY shell result exceeds bound");
}
static int start_shell(pid_t *pid) {
    int master=posix_openpt(O_RDWR|O_NOCTTY);
    nasd_require(master>=0,"open genuine terminal PTY master");
    nasd_require(grantpt(master)==0 && unlockpt(master)==0,
            "grant/unlock PTY slave");
    const char *name=ptsname(master);
    nasd_require(name!=NULL && strnlen(name,128)<128,"PTY slave name");
    char slave[128];strcpy(slave,name);
    *pid=fork();nasd_require(*pid>=0,"fork real terminal shell");
    if(!*pid) {
        if(setsid()<0) _exit(126);
        int fd=open(slave,O_RDWR);
        if(fd<0 || ioctl(fd,TIOCSCTTY,0)<0) _exit(125);
        if(dup2(fd,0)<0 || dup2(fd,1)<0 || dup2(fd,2)<0) _exit(124);
        if(fd>2)close(fd);
        close(master);close(3);
        /* Signed original dialog is invoked by the real guest shell on a
         * real PTY. The client verifies its output before announcing ready. */
        char *const argv[]={"busybox","sh","-c",
                           "/usr/bin/dialog --version 2>&1; "
                           "printf 'M167_SHELL_READY\\n'; "
                           "read typed; printf 'M167_SHELL_KEY=%s\\n' \"$typed\"",
                           NULL};
        execv("/bin/busybox",argv);
        _exit(123);
    }
    return master;
}
static void terminal_client(int channel) {
    int surface=-1;
    uint8_t *pixels=nasd_make_surface("m167-terminal",TERM_W,TERM_H,&surface);
    pid_t shell=0;
    int pty=start_shell(&shell);
    read_pty_until(pty,"M167_SHELL_READY",1);
    nasd_fill_surface(pixels,TERM_W,TERM_H,20,68,204);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    nasd_send_surface(channel,surface,TERMINAL);
    nasd_mark("M167_SIGNED_DIALOG_PTY_SHELL_PASS");
    for(;;) {
        struct message m=nasd_recv_packet(channel);
        if(m.type==MSG_QUIT)break;
        nasd_require(m.type==MSG_KEY && m.key==KEY_A,
                "terminal only accepts routed KEY_A");
        nasd_write_all(pty,"a\n",2);
        read_pty_until(pty,"M167_SHELL_KEY=a",0);
        int status=0;
        nasd_require(waitpid(shell,&status,0)==shell && WIFEXITED(status) &&
                WEXITSTATUS(status)==0,"BusyBox shell must exit successfully");
        shell=0;
        nasd_fill_surface(pixels,TERM_W,TERM_H,255,255,0);
        __atomic_thread_fence(__ATOMIC_RELEASE);
        nasd_send_packet(channel,MSG_UPDATED,KEY_A);
        nasd_mark("M167_REAL_PTY_KEY_ROUTE_PASS");
    }
    if(shell>0) {
        nasd_write_all(pty,"\n",1);
        (void)waitpid(shell,NULL,0);
    }
    close(pty);munmap(pixels,(size_t)TERM_W*TERM_H*4U);
    close(surface);close(channel);
}

int main(void) {
    terminal_client(3);
    return 0;
}
