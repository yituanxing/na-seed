#define _GNU_SOURCE
#include "protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

/* M167's first real multi-process USERSPACE desktop.  The parent alone owns
 * Linux framebuffer and evdev FDs. Two separately exec'ed clients create
 * Linux memfd-backed pixels and pass real open descriptions through AF_UNIX
 * SCM_RIGHTS. No compositor, cursor, UI or client protocol is in the kernel.
 * QMP supplies input and independently inspects the actual display surface.
 * The deliberately tiny local protocol is a bounded acceptance fixture, not
 * a purported implementation of Wayland/X11 or a public stable ABI. */
_Static_assert(sizeof(struct input_event) == 24, "Linux native LP64 evdev");
_Static_assert(sizeof(struct fb_var_screeninfo) == 160, "Linux framebuffer");
_Static_assert(sizeof(struct fb_fix_screeninfo) == 80, "Linux framebuffer");

struct window {
    int channel, surface_fd, x,y,w,h;
    uint8_t *pixels;
    pid_t pid;
};
struct input_handles {int keyboard,tablet,mouse; int abs_min[2],abs_max[2];};
struct session {
    struct window windows[WINDOW_COUNT];
    struct input_handles input;
    int fb, pointer_x,pointer_y,focus,top,stage;
    int held,dragging,grab_x,grab_y,origin_x,origin_y;
    int sent_key;
};
static uint8_t screen[SCREEN_W*SCREEN_H*4];

static int has_bit(const uint8_t *bits,unsigned count,unsigned number) {
    return number/8U<count && !!(bits[number/8U]&(1U<<(number%8U)));
}
static int has_code(int fd,unsigned type,unsigned code) {
    uint8_t bits[128]={0};
    nasd_require(ioctl(fd,EVIOCGBIT(type,sizeof(bits)),bits)>=0,
            "EVIOCGBIT hardware capability");
    return has_bit(bits,sizeof(bits),code);
}
static struct input_handles open_input(void) {
    struct input_handles h={.keyboard=-1,.tablet=-1,.mouse=-1};
    for(unsigned n=0;n<DEVICE_LIMIT;n++) {
        char path[40];snprintf(path,sizeof(path),"/dev/input/event%u",n);
        int fd=open(path,O_RDONLY|O_NONBLOCK);
        if(fd<0) {
            if(errno==ENOENT || errno==ENODEV)continue;
            nasd_fail("open real evdev provider");
        }
        uint8_t bits[8]={0};
        nasd_require(ioctl(fd,EVIOCGBIT(0,sizeof(bits)),bits)>=0,
                "EVIOCGBIT event types");
        if(h.keyboard<0 && has_bit(bits,sizeof(bits),EV_KEY) &&
           has_code(fd,EV_KEY,KEY_A) && has_code(fd,EV_KEY,KEY_Q))
            h.keyboard=fd;
        else if(h.tablet<0 && has_bit(bits,sizeof(bits),EV_ABS) &&
                has_code(fd,EV_ABS,ABS_X) && has_code(fd,EV_ABS,ABS_Y) &&
                has_code(fd,EV_KEY,BTN_LEFT))
            h.tablet=fd;
        else if(h.mouse<0 && has_bit(bits,sizeof(bits),EV_REL) &&
                has_code(fd,EV_REL,REL_X) && has_code(fd,EV_REL,REL_Y))
            h.mouse=fd;
        else nasd_require(close(fd)==0,"close unneeded input fd");
    }
    nasd_require(h.keyboard>=0 && h.tablet>=0 && h.mouse>=0,
            "three independent native input devices");
    struct input_absinfo x={0},y={0};
    nasd_require(ioctl(h.tablet,EVIOCGABS(ABS_X),&x)==0 &&
            ioctl(h.tablet,EVIOCGABS(ABS_Y),&y)==0 &&
            x.maximum>x.minimum && y.maximum>y.minimum,
            "real VirtIO tablet ABS min/max");
    h.abs_min[0]=x.minimum;h.abs_min[1]=y.minimum;
    h.abs_max[0]=x.maximum;h.abs_max[1]=y.maximum;
    nasd_mark("M167_NATIVE_EVDEV_DEVICES_PASS");
    return h;
}
static int open_fb(void) {
    int fd=open("/dev/fb0",O_RDWR);
    nasd_require(fd>=0,"open physical framebuffer");
    struct fb_var_screeninfo v={0};struct fb_fix_screeninfo f={0};
    nasd_require(ioctl(fd,FBIOGET_VSCREENINFO,&v)==0 &&
            ioctl(fd,FBIOGET_FSCREENINFO,&f)==0,
            "Linux framebuffer mode query");
    nasd_require(v.xres==SCREEN_W && v.yres==SCREEN_H &&
            v.bits_per_pixel==32 && v.red.offset==16 &&
            v.green.offset==8 && v.blue.offset==0 &&
            f.line_length==SCREEN_W*4,
            "physical 800x600 XRGB32 QEMU mode");
    return fd;
}
static void set_pixel(uint8_t *buffer,int x,int y,
                      uint8_t r,uint8_t g,uint8_t b) {
    size_t pos=((size_t)y*SCREEN_W+(unsigned)x)*4U;
    buffer[pos+0]=b;buffer[pos+1]=g;
    buffer[pos+2]=r;buffer[pos+3]=255;
}
static void draw_window(struct window *w,int focused) {
    /* Bounds are checked for every pixel; even dragging off the screen must
     * never turn a negative user-space coordinate into a host pointer. */
    for(int y=0;y<w->h+TITLE_H;y++) {
        int screen_y=w->y+y;
        if(screen_y<0 || screen_y>=SCREEN_H)continue;
        for(int x=0;x<w->w;x++) {
            int screen_x=w->x+x;
            if(screen_x<0 || screen_x>=SCREEN_W)continue;
            if(y<TITLE_H) {
                uint8_t r=focused?240:64;
                uint8_t g=focused?168:80;
                uint8_t b=focused?40:96;
                set_pixel(screen,screen_x,screen_y,r,g,b);
            } else {
                const size_t p=((size_t)(y-TITLE_H)*w->w+(unsigned)x)*4U;
                set_pixel(screen,screen_x,screen_y,w->pixels[p+2],
                          w->pixels[p+1],w->pixels[p+0]);
            }
        }
    }
}
static void render(struct session *s) {
    for(unsigned y=0;y<SCREEN_H;y++)
        for(unsigned x=0;x<SCREEN_W;x++)
            set_pixel(screen,(int)x,(int)y,16,32,48);
    draw_window(&s->windows[1-s->top],0);
    draw_window(&s->windows[s->top],1);
    /* The same positioned FileOps path is used by LVGL's Linux fbdev
     * non-mmap backend. No shared OpenFile seek cursor is modified. */
    size_t written=0;
    while(written<sizeof(screen)) {
        ssize_t n=pwrite(s->fb,screen+written,sizeof(screen)-written,
                         (off_t)written);
        if(n<0 && errno==EINTR)continue;
        nasd_require(n>0,"positioned framebuffer pwrite");
        written+=(size_t)n;
    }
}
static void spawn_client(struct session *s,int role) {
    const char *path=role==IMAGE?"/usr/bin/naseed-image":
                            "/usr/bin/naseed-terminal";
    int link[2]={-1,-1};
    nasd_require(socketpair(AF_UNIX,SOCK_STREAM,0,link)==0,
            "create genuine AF_UNIX client IPC");
    pid_t pid=fork();nasd_require(pid>=0,"fork desktop client");
    if(!pid) {
        close(link[0]);
        if(dup2(link[1],3)<0)_exit(122);
        /* Only a single IPC descriptor is inherited by the exec'ed client.
         * Devices, framebuffer and earlier clients remain desktop-owned. */
        for(int i=4;i<64;i++)close(i);
        char *const args[]={(char *)path,NULL};
        execv(path,args);
        _exit(121);
    }
    close(link[1]);
    struct window *w=&s->windows[role];
    w->pid=pid;w->channel=link[0];
    w->w=role==IMAGE?IMAGE_W:TERM_W;
    w->h=role==IMAGE?IMAGE_H:TERM_H;
    w->x=role==IMAGE?64:300;
    w->y=role==IMAGE?80:160;
    w->surface_fd=nasd_receive_surface(w->channel,role);
    const size_t bytes=(size_t)w->w*(size_t)w->h*4U;
    void *mapping=mmap(NULL,bytes,PROT_READ,MAP_SHARED,w->surface_fd,0);
    nasd_require(mapping!=MAP_FAILED,"map received shared surface in compositor");
    w->pixels=mapping;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    nasd_require(w->pixels[3]==255,"cross-process surface first pixel");
}
static int clamp(int v,int maximum) {
    return v<0?0:v>maximum?maximum:v;
}
static int scale_abs(int value,int min,int max,int pixels) {
    int64_t numerator=(int64_t)(value-min)*(pixels-1);
    return clamp((int)(numerator/(max-min)),pixels-1);
}
static int window_at(const struct session *s,int x,int y) {
    for(int pass=0;pass<WINDOW_COUNT;pass++) {
        int i=pass==0?s->top:1-s->top;
        const struct window *w=&s->windows[i];
        if(x>=w->x && y>=w->y && x<w->x+w->w &&
           y<w->y+w->h+TITLE_H)return i;
    }
    return -1;
}
static void pointer_button(struct session *s,int down) {
    /* QEMU may deliver BTN_LEFT through tablet OR mouse. One logical held
     * button is reported to this seat even when devices enumerate twice. */
    if(down==s->held)return;
    s->held=down;
    if(down) {
        int target=window_at(s,s->pointer_x,s->pointer_y);
        if(target<0)return;
        s->focus=s->top=target;
        struct window *w=&s->windows[target];
        s->dragging=s->pointer_y>=w->y &&
                    s->pointer_y<w->y+TITLE_H;
        s->grab_x=s->pointer_x;s->grab_y=s->pointer_y;
        s->origin_x=w->x;s->origin_y=w->y;
        render(s);
        if(s->stage==0 && target==IMAGE && !s->dragging) {
            s->stage=1;nasd_mark("M167_IMAGE_FOCUS_VISIBLE");
        } else if(s->stage==1 && target==IMAGE && s->dragging) {
            nasd_mark("M167_IMAGE_DRAG_GRABBED");
        } else if(s->stage==2 && target==TERMINAL) {
            s->stage=3;nasd_mark("M167_TERMINAL_FOCUS_VISIBLE");
        }
    } else if(s->dragging) {
        s->dragging=0;
        struct window *w=&s->windows[s->focus];
        if(s->stage==1 && s->focus==IMAGE &&
           w->x-s->origin_x>=50 && w->y-s->origin_y>=20) {
            render(s);s->stage=2;
            nasd_mark("M167_IMAGE_MOVED_VISIBLE");
        }
    }
}
static void pointer_motion(struct session *s) {
    if(!s->held || !s->dragging)return;
    struct window *w=&s->windows[s->focus];
    int x=s->origin_x+s->pointer_x-s->grab_x;
    int y=s->origin_y+s->pointer_y-s->grab_y;
    /* A compositor, not a frame buffer driver, determines window placement. */
    w->x=clamp(x,SCREEN_W-w->w);
    w->y=clamp(y,SCREEN_H-(w->h+TITLE_H));
    render(s);
}
static void input_event(struct session *s,unsigned device,
                        const struct input_event *e) {
    if(device==0 && e->type==EV_KEY && e->value==1) {
        if(e->code==KEY_Q && s->stage==4) {
            s->stage=5;
        } else if(e->code==KEY_A && s->stage==3 &&
                  s->focus==TERMINAL && !s->sent_key) {
            nasd_send_packet(s->windows[TERMINAL].channel,MSG_KEY,KEY_A);
            s->sent_key=1;
        }
    } else if(device==1 && e->type==EV_ABS) {
        if(e->code==ABS_X)
            s->pointer_x=scale_abs(e->value,s->input.abs_min[0],
                                    s->input.abs_max[0],SCREEN_W);
        if(e->code==ABS_Y)
            s->pointer_y=scale_abs(e->value,s->input.abs_min[1],
                                    s->input.abs_max[1],SCREEN_H);
        pointer_motion(s);
    } else if(device==2 && e->type==EV_REL) {
        if(e->code==REL_X)s->pointer_x=clamp(s->pointer_x+e->value,SCREEN_W-1);
        if(e->code==REL_Y)s->pointer_y=clamp(s->pointer_y+e->value,SCREEN_H-1);
        pointer_motion(s);
    }
    if(device!=0 && e->type==EV_KEY && e->code==BTN_LEFT &&
       (e->value==0 || e->value==1))pointer_button(s,e->value);
}
static void event_loop(struct session *s) {
    while(s->stage<5) {
        struct pollfd fds[5]={
            {.fd=s->input.keyboard,.events=POLLIN},
            {.fd=s->input.tablet,.events=POLLIN},
            {.fd=s->input.mouse,.events=POLLIN},
            {.fd=s->windows[IMAGE].channel,.events=POLLIN},
            {.fd=s->windows[TERMINAL].channel,.events=POLLIN}
        };
        int count=poll(fds,5,30000);
        if(count<0 && errno==EINTR)continue;
        nasd_require(count>0,"poll hardware devices and IPC timeout/nasd_fail");
        for(unsigned i=0;i<5;i++) {
            nasd_require(!(fds[i].revents&(POLLERR|POLLNVAL|POLLHUP)),
                    "desktop device or client disconnected");
            if(!(fds[i].revents&POLLIN))continue;
            if(i<3) {
                struct input_event events[32];
                ssize_t n=read(fds[i].fd,events,sizeof(events));
                if(n<0 && (errno==EAGAIN || errno==EINTR))continue;
                nasd_require(n>0 && n%(ssize_t)sizeof(events[0])==0,
                        "aligned LP64 hardware event read");
                for(size_t j=0;j<(size_t)n/sizeof(events[0]);j++)
                    input_event(s,i,&events[j]);
            } else {
                int role=(int)i-3;
                struct message m=nasd_recv_packet(s->windows[role].channel);
                nasd_require(role==TERMINAL && s->stage==3 &&
                        m.type==MSG_UPDATED && m.key==KEY_A,
                        "terminal-only live surface commit");
                __atomic_thread_fence(__ATOMIC_ACQUIRE);
                render(s);s->stage=4;
                nasd_mark("M167_TERMINAL_KEY_VISIBLE");
            }
        }
    }
}
static void desktop(void) {
    struct session s={0};
    s.input=open_input();
    s.fb=open_fb();
    spawn_client(&s,IMAGE);
    spawn_client(&s,TERMINAL);
    s.focus=s.top=TERMINAL;s.pointer_x=400;s.pointer_y=300;
    render(&s);
    nasd_mark("M167_DESKTOP_INITIAL_VISIBLE");
    event_loop(&s);
    for(unsigned i=0;i<WINDOW_COUNT;i++) {
        struct window *w=&s.windows[i];
        nasd_send_packet(w->channel,MSG_QUIT,0);
        int status=0;
        nasd_require(waitpid(w->pid,&status,0)==w->pid && WIFEXITED(status) &&
                WEXITSTATUS(status)==0,"independent client clean exit");
        munmap(w->pixels,(size_t)w->w*w->h*4U);
        close(w->channel);close(w->surface_fd);
    }
    close(s.input.keyboard);close(s.input.tablet);close(s.input.mouse);
    close(s.fb);
    nasd_mark("M167_TWO_EXEC_CLIENTS_CLEAN_PASS");
    nasd_mark("M167_DESKTOP_PASS");
}
/* Reproducible host check for EXACT production IPC/surface/geometry helpers.
 * No fake kernel entrypoint or fabricated guest acceptance marker. */
static void selftest(void) {
    int ends[2]={-1,-1};
    nasd_require(socketpair(AF_UNIX,SOCK_STREAM,0,ends)==0,"host IPC socketpair");
    pid_t child=fork();nasd_require(child>=0,"host IPC fork");
    if(!child) {
        close(ends[0]);
        int fd=-1;
        uint8_t *pixels=nasd_make_surface("m167-selftest-image",IMAGE_W,
                                      IMAGE_H,&fd);
        nasd_fill_surface(pixels,IMAGE_W,IMAGE_H,255,0,255);
        nasd_send_surface(ends[1],fd,IMAGE);
        munmap(pixels,(size_t)IMAGE_W*IMAGE_H*4U);
        close(fd);close(ends[1]);
        _exit(0);
    }
    close(ends[1]);
    int fd=nasd_receive_surface(ends[0],IMAGE);
    uint8_t *pixels=mmap(NULL,(size_t)IMAGE_W*IMAGE_H*4U,
                         PROT_READ,MAP_SHARED,fd,0);
    nasd_require(pixels!=MAP_FAILED && pixels[0]==255 && pixels[1]==0 &&
            pixels[2]==255 && pixels[3]==255,
            "cross-process memfd SCM_RIGHTS pixel identity");
    int status=0;
    nasd_require(waitpid(child,&status,0)==child && WIFEXITED(status) &&
            WEXITSTATUS(status)==0,"independent IPC client exit");
    struct session s={0};
    s.windows[IMAGE]=(struct window){.x=64,.y=80,.w=IMAGE_W,.h=IMAGE_H,
                                      .pixels=pixels};
    s.windows[TERMINAL]=(struct window){.x=300,.y=160,
                     .w=TERM_W,.h=TERM_H};
    int tfd=-1;
    uint8_t *terminal=nasd_make_surface("m167-selftest-terminal",TERM_W,
                                     TERM_H,&tfd);
    nasd_fill_surface(terminal,TERM_W,TERM_H,20,68,204);
    s.windows[TERMINAL].pixels=terminal;
    s.top=TERMINAL;
    draw_window(&s.windows[IMAGE],0);
    draw_window(&s.windows[TERMINAL],1);
    const size_t overlap=((size_t)210*SCREEN_W+340)*4U;
    nasd_require(screen[overlap+2]==20 && screen[overlap+1]==68 &&
            screen[overlap]==204,"bottom image clipped by terminal top");
    s.top=IMAGE;
    draw_window(&s.windows[TERMINAL],0);
    draw_window(&s.windows[IMAGE],1);
    nasd_require(screen[overlap+2]==255 && screen[overlap+1]==0 &&
            screen[overlap]==255,"focus reorders surface stack");
    nasd_require(window_at(&s,150,150)==IMAGE &&
            window_at(&s,550,220)==TERMINAL,
            "compositor topmost hit-testing");
    nasd_require(scale_abs(0,0,32767,SCREEN_W)==0 &&
            scale_abs(32767,0,32767,SCREEN_W)==799,
            "Linux tablet axis scaling is bounded");
    nasd_fill_surface(terminal,TERM_W,TERM_H,255,255,0);
    draw_window(&s.windows[TERMINAL],1);
    const size_t sample=((size_t)300*SCREEN_W+500)*4U;
    nasd_require(screen[sample+2]==255 && screen[sample+1]==255 &&
            screen[sample]==0,"live shared terminal buffer repaint");
    munmap(pixels,(size_t)IMAGE_W*IMAGE_H*4U);
    munmap(terminal,(size_t)TERM_W*TERM_H*4U);
    close(fd);close(tfd);close(ends[0]);
    nasd_mark("[M167 HOST DESKTOP PASS] fork/SCM_RIGHTS/memfd shared pixels, "
         "overlap, focus, hit-testing, axis scaling, live surface update");
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--selftest")) {
        selftest();return 0;
    }
    /* The signed guest program runner passes its case identifier as argv[1].
     * Desktop can also be launched interactively without that selector. */
    nasd_require(argc==1 || (argc==2 && !strcmp(argv[1],"m167")),
                 "desktop mode arguments");
    desktop();
    return 0;
}
