#define _GNU_SOURCE
#define NASD_VT100_IMPLEMENTATION
#include "../user/desktop/vt100.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

/* Independent host oracle: feed the ACTUAL installed GNU nano executable's
 * curses/terminfo PTY stream into the production VT implementation. No
 * generated ANSI fixture is substituted for these original application bytes.
 * Guest original signed Alpine acceptance is a separate physical QMP gate. */
static int screen_contains(const struct vt100 *v,const char *needle) {
    const size_t n=strlen(needle);
    for(unsigned row=0;row<v->rows;row++)for(unsigned col=0;col+n<=v->cols;col++) {
        size_t i=0;
        while(i<n && v->pages[v->alternate][row][col+i].ch==
                      (unsigned char)needle[i])i++;
        if(i==n)return 1;
    }
    return 0;
}
int main(void) {
    if(access("/usr/bin/nano",X_OK)!=0) {
        fputs("[M187 HOST FULLSCREEN FAIL] GNU nano host binary unavailable\n",stderr);
        return 77;
    }
    int master=posix_openpt(O_RDWR|O_NOCTTY|O_NONBLOCK);
    assert(master>=0 && grantpt(master)==0 && unlockpt(master)==0);
    const char *name=ptsname(master);
    assert(name!=NULL);
    char slave[128];
    assert(strlen(name)<sizeof(slave));strcpy(slave,name);
    pid_t pid=fork();assert(pid>=0);
    if(!pid) {
        assert(setsid()>0);
        int fd=open(slave,O_RDWR);assert(fd>=0);
        assert(ioctl(fd,TIOCSCTTY,0)==0);
        const struct winsize size={.ws_row=VT100_ROWS,.ws_col=VT100_COLS};
        assert(ioctl(fd,TIOCSWINSZ,&size)==0);
        assert(dup2(fd,0)>=0 && dup2(fd,1)>=0 && dup2(fd,2)>=0);
        if(fd>2)close(fd);
        close(master);
        setenv("TERM","vt100",1);setenv("LC_ALL","C",1);
        char *const argv[]={"nano","--ignorercfiles","tests/nano-note.txt",NULL};
        execv("/usr/bin/nano",argv);
        _exit(127);
    }
    struct vt100 screen;
    vt100_init(&screen,VT100_COLS,VT100_ROWS);
    int seen_title=0,seen_file=0,seen_csi=0;
    size_t total=0;
    for(unsigned turn=0;turn<100;turn++) {
        struct pollfd fd={.fd=master,.events=POLLIN};
        int polled=poll(&fd,1,100);
        if(polled<0 && errno==EINTR)continue;
        if(polled<0)break;
        if(polled>0 && (fd.revents&POLLIN)) {
            for(;;) {
                char output[2048];
                ssize_t n=read(master,output,sizeof(output));
                if(n<0 && errno==EINTR)continue;
                if(n<0 && (errno==EAGAIN||errno==EWOULDBLOCK))break;
                if(n<=0)break;
                total+=(size_t)n;
                if(memchr(output,27,(size_t)n))seen_csi=1;
                vt100_feed(&screen,output,(size_t)n);
                char answer[128];
                size_t response=vt100_take_reply(&screen,answer,sizeof(answer));
                if(response)assert(write(master,answer,response)==(ssize_t)response);
            }
            if(screen_contains(&screen,"GNU nano"))seen_title=1;
            if(screen_contains(&screen,"NaSeed nano editor"))seen_file=1;
            if(seen_title && seen_file)break;
        }
    }
    struct winsize observed={0};
    assert(ioctl(master,TIOCGWINSZ,&observed)==0 &&
           observed.ws_row==VT100_ROWS && observed.ws_col==VT100_COLS);
    /* Nano exits on the same physical key byte emitted by terminal_key_bytes
     * for Ctrl+X. This test is not satisfied by --version/-h. */
    char ctrl_x=24;
    assert(write(master,&ctrl_x,1)==1);
    int status=0,exited=0;
    for(unsigned i=0;i<100;i++) {
        pid_t n=waitpid(pid,&status,WNOHANG);
        if(n==pid) {exited=1;break;}
        if(n<0)break;
        struct pollfd fd={.fd=master,.events=POLLIN};
        (void)poll(&fd,1,100);
        if(fd.revents&POLLIN) {
            char discard[2048];(void)read(master,discard,sizeof(discard));
        }
    }
    if(!exited) {kill(pid,SIGKILL);(void)waitpid(pid,&status,0);}
    close(master);
    if(!(seen_csi && seen_title && seen_file && exited && WIFEXITED(status) &&
         WEXITSTATUS(status)==0)) {
        fprintf(stderr,"[M187 HOST FULLSCREEN FAIL] bytes=%zu ESC=%d "
                "title=%d file=%d exited=%d status=%d\n",
                total,seen_csi,seen_title,seen_file,exited,status);
        for(unsigned row=0;row<screen.rows;row++) {
            fprintf(stderr,"row %02u: ",row);
            for(unsigned col=0;col<screen.cols;col++)
                fputc(screen.pages[screen.alternate][row][col].ch,stderr);
            fputc('\n',stderr);
        }
        return 1;
    }
    puts("[M187 HOST FULLSCREEN PASS] original GNU nano interactive ncurses "
         "on real PTY, ioctl 53x25 VT cells, original file contents, Ctrl+X exit");
    return 0;
}
