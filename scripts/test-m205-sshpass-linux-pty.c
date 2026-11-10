/* Linux-host oracle for sshpass's documented controlling-tty contract.
 * sshpass's child does setsid/open(pts)/close(pts), NOT dup2(pts,0/1).
 * The original M205 test incorrectly printed to stdout and read /dev/null.
 * These assertions use actual Linux /dev/ptmx and /dev/tty, no emulation. */
#define _XOPEN_SOURCE 600
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

enum probe_mode { PROBE_CORRECT,PROBE_BAD_PASSWORD,PROBE_OLD_STDIO,
                  PROBE_NOCTTY };
static void run_case(enum probe_mode mode) {
    int master=posix_openpt(O_RDWR|O_NOCTTY);
    assert(master>=0);
    assert(grantpt(master)==0 && unlockpt(master)==0);
    const char *name=ptsname(master);
    assert(name!=NULL);
    char path[80];
    assert(strlen(name)<sizeof(path));
    strcpy(path,name);

    pid_t child=fork();
    assert(child>=0);
    if(child==0) {
        if(setsid()<0)_exit(101);
        int slave=open(path,O_RDWR|(mode==PROBE_NOCTTY?O_NOCTTY:0));
        if(slave<0)_exit(102);
        close(slave); /* sshpass immediately closes the slave. */
        int ttyfd=open("/dev/tty",O_RDWR|O_NOCTTY);
        if(mode==PROBE_NOCTTY)
            _exit(ttyfd<0 && errno==ENXIO ? 0:103);
        if(ttyfd<0)_exit(104);
        if(mode==PROBE_OLD_STDIO) {
            /* The old M205 shell inherited fd0=/dev/null and sent its
             * prompt to redirected stdout. Neither fd reaches the PTY. */
            int devnull=open("/dev/null",O_RDWR);
            if(devnull<0 || dup2(devnull,0)<0 ||
               dup2(devnull,1)<0)_exit(105);
            close(devnull);
            if(write(1,"Password:",9)!=9)_exit(106);
            char c=0;
            _exit(read(0,&c,1)==0?0:107);
        }
        if(write(ttyfd,"Password:",9)!=9)_exit(108);
        char received[80];
        size_t length=0;
        while(length+1<sizeof(received)) {
            ssize_t n=read(ttyfd,received+length,1);
            if(n!=1)_exit(109);
            if(received[length++]=='\n')break;
        }
        received[length]=0;
        _exit(strcmp(received,"m205-secret\n")==0?0:19);
    }

    if(mode!=PROBE_NOCTTY && mode!=PROBE_OLD_STDIO) {
        struct pollfd pfd={.fd=master,.events=POLLIN};
        assert(poll(&pfd,1,3000)>0 && (pfd.revents&POLLIN));
        char prompt[24]={0};
        assert(read(master,prompt,sizeof(prompt))==9);
        assert(strcmp(prompt,"Password:")==0);
        const char *reply=mode==PROBE_CORRECT ?
            "m205-secret\n":"incorrect\n";
        assert(write(master,reply,strlen(reply))==(ssize_t)strlen(reply));
    }
    int status=0;
    assert(waitpid(child,&status,0)==child && WIFEXITED(status));
    assert(WEXITSTATUS(status)==(mode==PROBE_BAD_PASSWORD?19:0));
    if(mode==PROBE_OLD_STDIO) {
        /* An exited old-style child never put a prompt on the PTY. */
        struct pollfd pfd={.fd=master,.events=POLLIN};
        if(poll(&pfd,1,0)>0)assert(!(pfd.revents&POLLIN));
    }
    close(master);
}
int main(void) {
    run_case(PROBE_OLD_STDIO);
    run_case(PROBE_NOCTTY);
    run_case(PROBE_BAD_PASSWORD);
    run_case(PROBE_CORRECT);
    puts("[M205 LINUX SSHPASS PTY CONTRACT PASS] old stdio impossible, O_NOCTTY rejected, wrong password rejected, /dev/tty exchange succeeds");
    return 0;
}
