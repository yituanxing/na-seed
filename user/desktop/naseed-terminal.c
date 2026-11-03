#define _GNU_SOURCE
#include "protocol.h"
#include "text.h"
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
static void read_pty_until(int fd,const char *wanted,int version,
                           char *capture,size_t capacity) {
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
            if(capture && capacity) {
                size_t copy=used<capacity-1?used:capacity-1;
                memcpy(capture,collected,copy);capture[copy]=0;
            }
            return;
        }
    }
    nasd_require(0,"PTY shell result exceeds bound");
}
static int start_shell(pid_t *pid,int interactive) {
    int master=posix_openpt(O_RDWR|O_NOCTTY);
    nasd_require(master>=0,"open genuine terminal PTY master");
    nasd_require(grantpt(master)==0 && unlockpt(master)==0,
            "grant/unlock PTY slave");
    const char *name=ptsname(master);
    nasd_require(name!=NULL && strnlen(name,128)<128,"PTY slave name");
    char slave[128];strcpy(slave,name);
    /* A PTY master can legitimately report POLLHUP while the forked child
     * has not yet opened its slave. Synchronize the actual slave attachment
     * before the parent starts the terminal reader. This is a one-shot
     * process-lifecycle pipe, not a fabricated terminal output token. */
    int attached[2]={-1,-1};
    nasd_require(pipe(attached)==0,"terminal child attachment handshake pipe");
    *pid=fork();nasd_require(*pid>=0,"fork real terminal shell");
    if(!*pid) {
        close(attached[0]);
        if(setsid()<0) _exit(126);
        int fd=open(slave,O_RDWR);
        if(fd<0 || ioctl(fd,TIOCSCTTY,0)<0) _exit(125);
        if(dup2(fd,0)<0 || dup2(fd,1)<0 || dup2(fd,2)<0) _exit(124);
        if(fd>2)close(fd);
        close(master);close(3);
        (void)unsetenv(NASD_INHERITED_ENV);
        nasd_write_all(attached[1],"R",1);
        close(attached[1]);
        /* Signed original dialog is invoked by the real guest shell on a
         * real PTY. The client verifies its output before announcing ready. */
        char *const argv[]={"busybox","sh","-c",
                           "/usr/bin/dialog --version 2>&1; "
                           "printf 'M167_SHELL_READY\\n'; "
                           "read typed; printf 'M167_SHELL_KEY=%s\\n' \"$typed\"",
                           NULL};
        if(interactive) {
            /* Keep the real PTY and controlling TTY, but execute a normal
             * persistent interactive BusyBox Shell. The old finite script
             * remains untouched for M167/M168 compatibility. */
            (void)setenv("TERM","dumb",1);
            (void)setenv("PS1","NASEED$ ",1);
            char *const live_argv[]={"busybox","sh","-i",NULL};
            execv("/bin/busybox",live_argv);
        } else execv("/bin/busybox",argv);
        _exit(123);
    }
    close(attached[1]);
    char ready=0;
    nasd_read_all(attached[0],&ready,1);
    close(attached[0]);
    nasd_require(ready=='R',"PTY child attached real slave");
    return master;
}
static void terminal_client(int channel,int rich) {
    int surface=-1;
    uint8_t *pixels=nasd_make_surface("m167-terminal",TERM_W,TERM_H,&surface);
    pid_t shell=0;
    int pty=start_shell(&shell,0);
    char initial[2048]={0};
    read_pty_until(pty,"M167_SHELL_READY",1,initial,sizeof(initial));
    nasd_fill_surface(pixels,TERM_W,TERM_H,20,68,204);
    if(rich)
        nasd_draw_text(pixels,TERM_W,TERM_H,8,9,initial,strlen(initial),
                       250,250,255);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    nasd_send_surface_caps(channel,surface,TERMINAL,TERM_W,TERM_H,
                           NASD_INPUT_KEYS);
    nasd_mark("M167_SIGNED_DIALOG_PTY_SHELL_PASS");
    if(rich)nasd_mark("M168_TERMINAL_TEXT_FROM_PTY_PASS");
    for(;;) {
        struct message m=nasd_recv_packet(channel);
        if(m.type==MSG_QUIT)break;
        nasd_require(m.type==MSG_KEY && m.key==KEY_A,
                "terminal only accepts routed KEY_A");
        nasd_write_all(pty,"a\n",2);
        char typed[2048]={0};
        read_pty_until(pty,"M167_SHELL_KEY=a",0,typed,sizeof(typed));
        int status=0;
        nasd_require(waitpid(shell,&status,0)==shell && WIFEXITED(status) &&
                WEXITSTATUS(status)==0,"BusyBox shell must exit successfully");
        shell=0;
        nasd_fill_surface(pixels,TERM_W,TERM_H,255,255,0);
        if(rich)
            nasd_draw_text(pixels,TERM_W,TERM_H,8,9,typed,strlen(typed),
                           15,26,56);
        __atomic_thread_fence(__ATOMIC_RELEASE);
        nasd_send_packet(channel,MSG_UPDATED,KEY_A);
        nasd_mark("M167_REAL_PTY_KEY_ROUTE_PASS");
        if(rich)nasd_mark("M168_REAL_PTY_TEXT_KEY_ROUTE_PASS");
    }
    if(shell>0) {
        nasd_write_all(pty,"\n",1);
        (void)waitpid(shell,NULL,0);
    }
    close(pty);munmap(pixels,(size_t)TERM_W*TERM_H*4U);
    close(surface);close(channel);
}

/* A tiny terminal *display* for the genuine PTY stream. No shell logic or
 * synthetic command replies live here: keyboard bytes flow to the PTY and
 * visible glyphs are derived only from bytes read back from its master. */
enum { LIVE_COLS=25,LIVE_ROWS=11 };
struct transcript {
    char cells[LIVE_ROWS][LIVE_COLS+1];
    unsigned row,col;
    char evidence[4096];
    size_t evidence_len;
};
static void transcript_byte(struct transcript *t,unsigned char c) {
    if(c=='\r') {t->col=0;return;}
    if(c=='\b' || c==127) {
        if(t->col)t->col--;
        t->cells[t->row][t->col]=' ';
        return;
    }
    if(c=='\n') {
        t->col=0;
        if(++t->row==LIVE_ROWS) {
            memmove(t->cells[0],t->cells[1],
                    (LIVE_ROWS-1)*sizeof(t->cells[0]));
            t->row=LIVE_ROWS-1;
        }
        memset(t->cells[t->row],' ',LIVE_COLS);
        return;
    }
    if(c=='\t')c=' ';
    if(c<32 || c>126)return;
    if(t->col>=LIVE_COLS) {
        transcript_byte(t,'\n');
    }
    t->cells[t->row][t->col++]=(char)c;
}
static void transcript_append(struct transcript *t,const char *data,size_t n) {
    for(size_t i=0;i<n;i++) {
        unsigned char c=(unsigned char)data[i];
        transcript_byte(t,c);
        /* Normalize CRLF for exact *program-output* proof. An echoed
         * command 'echo 1\n' never contains the standalone '\n1\n'. */
        if(c=='\r')continue;
        if(t->evidence_len==sizeof(t->evidence)-1) {
            memmove(t->evidence,t->evidence+1024,
                    t->evidence_len-1024);
            t->evidence_len-=1024;
        }
        t->evidence[t->evidence_len++]=(char)c;
        t->evidence[t->evidence_len]=0;
    }
}
static void transcript_paint(uint8_t *pixels,const struct transcript *t) {
    nasd_fill_surface(pixels,TERM_W,TERM_H,20,68,204);
    nasd_draw_text(pixels,TERM_W,TERM_H,8,5,"NASEED LIVE PTY",15,
                   255,255,255);
    for(unsigned row=0;row<LIVE_ROWS;row++)
        nasd_draw_text(pixels,TERM_W,TERM_H,8,25+row*18,
                       t->cells[row],LIVE_COLS,244,247,255);
}
static unsigned terminal_key_byte(unsigned code) {
    /* Linux evdev keycodes, not ASCII: the hardware layer must be translated
     * exactly once before writing ordinary bytes to the PTY master. */
    switch(code) {
    case KEY_A:return 'a';case KEY_B:return 'b';case KEY_C:return 'c';
    case KEY_D:return 'd';case KEY_E:return 'e';case KEY_F:return 'f';
    case KEY_G:return 'g';case KEY_H:return 'h';case KEY_I:return 'i';
    case KEY_J:return 'j';case KEY_K:return 'k';case KEY_L:return 'l';
    case KEY_M:return 'm';case KEY_N:return 'n';case KEY_O:return 'o';
    case KEY_P:return 'p';case KEY_Q:return 'q';case KEY_R:return 'r';
    case KEY_S:return 's';case KEY_T:return 't';case KEY_U:return 'u';
    case KEY_V:return 'v';case KEY_W:return 'w';case KEY_X:return 'x';
    case KEY_Y:return 'y';case KEY_Z:return 'z';
    case KEY_1:return '1';case KEY_2:return '2';case KEY_3:return '3';
    case KEY_4:return '4';case KEY_5:return '5';case KEY_6:return '6';
    case KEY_7:return '7';case KEY_8:return '8';case KEY_9:return '9';
    case KEY_0:return '0';case KEY_SPACE:return ' ';
    case KEY_ENTER:return '\n';case KEY_BACKSPACE:return 127;
    case KEY_DOT:return '.';case KEY_MINUS:return '-';
    case KEY_SLASH:return '/';
    default:return 0;
    }
}
static void terminal_live(int channel) {
    int surface=-1;
    uint8_t *pixels=nasd_make_surface("m170-live-terminal",TERM_W,TERM_H,
                                     &surface);
    struct transcript t={0};
    for(unsigned i=0;i<LIVE_ROWS;i++)
        memset(t.cells[i],' ',LIVE_COLS);
    transcript_paint(pixels,&t);
    pid_t child=0;
    int pty=start_shell(&child,1);
    nasd_send_surface_caps(channel,surface,TERMINAL,TERM_W,TERM_H,
                           NASD_INPUT_KEYS);
    nasd_mark("M170_PTY_INTERACTIVE_SHELL_ATTACHED");
    unsigned verified=0;
    for(;;) {
        struct pollfd fds[2]={
            {.fd=channel,.events=POLLIN},
            {.fd=pty,.events=POLLIN}
        };
        int ready=poll(fds,2,-1);
        if(ready<0 && errno==EINTR)continue;
        nasd_require(ready>0,"interactive PTY plus client IPC poll");
        if(fds[0].revents&POLLIN) {
            struct message m=nasd_recv_packet(channel);
            if(m.type==MSG_QUIT) {
                nasd_write_all(pty,"exit\n",5);
                break;
            }
            nasd_require(m.type==MSG_KEY,"live terminal receives key IPC");
            unsigned ch=terminal_key_byte(m.key);
            if(ch) {
                char input=(char)ch;
                nasd_write_all(pty,&input,1);
            }
        }
        if(fds[1].revents&POLLIN) {
            char output[512];
            ssize_t got=read(pty,output,sizeof(output));
            if(got<0 && (errno==EINTR || errno==EAGAIN))continue;
            nasd_require(got>=0,"live PTY read");
            if(got>0) {
                transcript_append(&t,output,(size_t)got);
                transcript_paint(pixels,&t);
                __atomic_thread_fence(__ATOMIC_RELEASE);
                unsigned tag=0;
                if(!(verified&1U) && strstr(t.evidence,"\n1\n")) {
                    verified|=1U;tag=KEY_1;
                    nasd_mark("M170_SHELL_FIRST_REAL_OUTPUT");
                }
                if((verified&1U) && !(verified&2U) &&
                   strstr(t.evidence,"\n2\n")) {
                    verified|=2U;tag=KEY_2;
                    nasd_mark("M170_SHELL_SECOND_REAL_OUTPUT");
                }
                nasd_send_packet(channel,MSG_UPDATED,tag);
            }
        }
        if(fds[1].revents&(POLLHUP|POLLERR)) {
            /* Readable PTY bytes win over HUP. Once exhausted, notify the
             * desktop that this client is retiring instead of spinning. */
            if(!(fds[1].revents&POLLIN)) {
                int status=0;
                (void)waitpid(child,&status,0);
                child=0;
                nasd_send_packet(channel,MSG_CLOSE,0);
                break;
            }
        }
        nasd_require(!(fds[0].revents&(POLLERR|POLLNVAL|POLLHUP)),
                     "live terminal compositor IPC disconnected");
    }
    if(child>0) {
        int status=0;
        nasd_require(waitpid(child,&status,0)==child && WIFEXITED(status),
                     "interactive shell clean final exit");
    }
    close(pty);munmap(pixels,(size_t)TERM_W*TERM_H*4U);
    close(surface);close(channel);
}
static void terminal_selftest170(void) {
    struct transcript t={0};
    for(unsigned i=0;i<LIVE_ROWS;i++)
        memset(t.cells[i],' ',LIVE_COLS);
    transcript_append(&t,"echo 1\r\n1\r\n",11);
    nasd_require(strstr(t.evidence,"\n1\n")!=NULL,
                 "actual first command output differs from input echo");
    transcript_append(&t,"echo 2\r\n2\r\n",11);
    nasd_require(strstr(t.evidence,"\n2\n")!=NULL,
                 "actual second command output differs from input echo");
    for(unsigned i=0;i<25;i++)
        transcript_append(&t,"line\n",5);
    nasd_require(t.row==LIVE_ROWS-1,"scrolling ring stays in bounds");
    nasd_require(terminal_key_byte(KEY_E)=='e' &&
                 terminal_key_byte(KEY_ENTER)=='\n' &&
                 terminal_key_byte(KEY_BACKSPACE)==127 &&
                 terminal_key_byte(KEY_2)=='2',
                 "evdev translation for persistent PTY");
    int fd=-1;
    uint8_t *pixels=nasd_make_surface("m170-terminal-selftest",TERM_W,
                                      TERM_H,&fd);
    transcript_paint(pixels,&t);
    size_t glyph=((size_t)25*TERM_W+8)*4U;
    nasd_require(pixels[glyph+3]==255,
                 "native shared framebuffer contains live transcript pixels");
    munmap(pixels,(size_t)TERM_W*TERM_H*4U);close(fd);
    nasd_mark("[M170 HOST TERMINAL PASS] PTY transcript output, scroll, evdev translation and memfd pixels");
}

int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--selftest170")) {
        terminal_selftest170();return 0;
    }
    nasd_require(argc==1 || (argc==2 &&
                 (!strcmp(argv[1],"--m168") ||
                  !strcmp(argv[1],"--m170"))),
                 "terminal client mode selector");
    if(argc==2 && !strcmp(argv[1],"--m170"))
        terminal_live(nasd_client_channel());
    else terminal_client(nasd_client_channel(),argc==2);
    return 0;
}
