#define _GNU_SOURCE
#include "protocol.h"
#include "text.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Independent userspace document viewer. Original APK-signed Lynx parses a
 * real HTML file and Midnight Commander reports its version; their output
 * becomes actual client-owned pixels (not compositor-generated labels). */
static size_t signed_program_output(int channel,const char *path,
                                    char *const argv[],char *out,size_t cap) {
    int pipefd[2]={-1,-1};
    nasd_require(pipe(pipefd)==0,"signed document tool output pipe");
    pid_t pid=fork();nasd_require(pid>=0,"fork signed document tool");
    if(!pid) {
        close(channel);close(pipefd[0]);
        if(dup2(pipefd[1],STDOUT_FILENO)<0 ||
           dup2(pipefd[1],STDERR_FILENO)<0)_exit(125);
        close(pipefd[1]);
        execv(path,argv);_exit(127);
    }
    close(pipefd[1]);
    /* Consume the entire pipe, even when the display buffer fills. A full
     * buffer must not truncate the pipe reader and deadlock waitpid(). */
    nasd_require(cap>0,"signed document output buffer");
    size_t used=0;
    char chunk[512];
    for(;;) {
        ssize_t n=read(pipefd[0],chunk,sizeof(chunk));
        if(n<0 && errno==EINTR)continue;
        nasd_require(n>=0,"signed program output read");
        if(!n)break;
        size_t keep=(size_t)n;
        if(keep>cap-1-used)keep=cap-1-used;
        if(keep)memcpy(out+used,chunk,keep);
        used+=keep;
    }
    out[used]=0;
    close(pipefd[0]);
    int status=0;
    nasd_require(waitpid(pid,&status,0)==pid && WIFEXITED(status) &&
                 WEXITSTATUS(status)==0,"original signed document program exit");
    return used;
}
static void paint(uint8_t *pixels,const char *lynx,size_t lynx_size,
                  const char *mc,size_t mc_size,int active) {
    nasd_fill_surface(pixels,BROWSER_W,BROWSER_H,
                      active?218:34,active?115:95,active?48:72);
    const char *banner=active?"BROWSER KEY B RECEIVED":"LYNX HTML AND MC VIEW";
    nasd_draw_text(pixels,BROWSER_W,BROWSER_H,8,7,banner,strlen(banner),
                   255,255,255);
    nasd_draw_text(pixels,BROWSER_W,BROWSER_H,8,27,lynx,lynx_size,
                   245,247,235);
    /* Keep the signed MC identification visible beneath the Lynx text. */
    nasd_draw_text(pixels,BROWSER_W,BROWSER_H,8,154,mc,mc_size,
                   245,239,219);
}
int main(int argc,char **argv) {
    const int channel=3;
    const int live=argc==2 && !strcmp(argv[1],"--m170");
    nasd_require(argc==1 || live,"browser client selector");
    char lynx[2048]={0},mc[4096]={0};
    char *const lynx_argv[]={"lynx","-dump","-nolist","-width=38",
                              "/tests/m168-home.html",NULL};
    size_t nlynx=signed_program_output(channel,"/usr/bin/lynx",
                                      lynx_argv,lynx,sizeof(lynx));
    nasd_require(strstr(lynx,"NaSeed")!=NULL,
                 "original Lynx did not render HTML content");
    char *const mc_argv[]={"mc","--version",NULL};
    size_t nmc=signed_program_output(channel,"/usr/bin/mc",
                                    mc_argv,mc,sizeof(mc));
    nasd_require(strstr(mc,"Midnight Commander")!=NULL,
                 "original MC version output unavailable");
    int surface=-1;
    uint8_t *pixels=nasd_make_surface("m168-browser",BROWSER_W,
                                     BROWSER_H,&surface);
    paint(pixels,lynx,nlynx,mc,nmc,0);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    nasd_send_surface(channel,surface,BROWSER,BROWSER_W,BROWSER_H);
    nasd_mark("M168_SIGNED_LYNX_MC_BROWSER_PASS");
    for(;;) {
        struct message msg=nasd_recv_packet(channel);
        if(msg.type==MSG_QUIT)break;
        nasd_require(msg.type==MSG_KEY &&
                     (msg.key==KEY_B || (live && msg.key==KEY_C)),
                     "focused browser legal keyboard input");
        if(live && msg.key==KEY_C) {
            nasd_send_packet(channel,MSG_CLOSE,0);
            nasd_mark("M170_BROWSER_CLIENT_CLOSE_SENT");
            break;
        }
        paint(pixels,lynx,nlynx,mc,nmc,1);
        __atomic_thread_fence(__ATOMIC_RELEASE);
        nasd_send_packet(channel,MSG_UPDATED,KEY_B);
        nasd_mark("M168_REAL_BROWSER_KEY_ROUTE_PASS");
    }
    munmap(pixels,(size_t)BROWSER_W*BROWSER_H*4U);
    close(surface);close(channel);
    return 0;
}
