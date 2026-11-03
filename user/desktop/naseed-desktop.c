#define _GNU_SOURCE
#include "protocol.h"
#include "text.h"
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
#include <sys/un.h>
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
    int channel, surface_fd, x,y,w,h,role;
    uint8_t *pixels;
    pid_t pid;
    int active,pending,owned_child;
    uint32_t capabilities;
    struct nasd_surface_offer offer;
    struct nasd_packet_reader incoming;
};
struct input_handles {int keyboard,tablet,mouse; int abs_min[2],abs_max[2];};
struct session {
    struct window windows[WINDOW_LIMIT];
    unsigned order[WINDOW_LIMIT],count,live_count;
    struct input_handles input;
    int fb,listener,pointer_x,pointer_y,focus,top,stage,mode;
    int held,dragging,grab_x,grab_y,origin_x,origin_y;
    int sent_key,exit_requested,pointer_client;
    unsigned modifier_keys,vt_keyboard;
    unsigned changes,external_live,external_total;
};
struct client_spec {const char *path;int x,y;};
static int dynamic_mode(int mode) {return mode==173 || mode==187;}
static const struct client_spec clients[]={
    [IMAGE]={"/usr/bin/naseed-image",64,80},
    [TERMINAL]={"/usr/bin/naseed-terminal",300,160},
    [BROWSER]={"/usr/bin/naseed-browser",445,86},
    [LVGL]={"/usr/bin/naseed-lvgl",75,365},
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
    for(unsigned i=0;i<s->live_count;i++) {
        unsigned index=s->order[i];
        nasd_require(index<s->count,"window z-order index");
        draw_window(&s->windows[index],(int)index==s->focus);
    }
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
static int attach_surface(struct window *w,int fd,unsigned width,
                          unsigned height) {
    if(width>SCREEN_W || height>SCREEN_H-TITLE_H ||
       w->x<0 || w->y<0 || w->x+(int)width>SCREEN_W ||
       w->y+(int)height+TITLE_H>SCREEN_H)return -1;
    const size_t bytes=(size_t)width*height*4U;
    void *mapping=mmap(NULL,bytes,PROT_READ,MAP_SHARED,fd,0);
    if(mapping==MAP_FAILED)return -1;
    w->surface_fd=fd;w->w=(int)width;w->h=(int)height;
    w->pixels=mapping;w->active=1;w->pending=0;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return 0;
}
static int open_listener(void) {
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);
    nasd_require(fd>=0,"create userspace desktop listener");
    struct sockaddr_un address={.sun_family=AF_UNIX};
    const char *path=getenv(NASD_SOCKET_ENV);
    if(!path || !*path)path=NASD_SOCKET_PATH;
    nasd_require(strlen(path)<sizeof(address.sun_path),
                 "desktop listener path bounded");
    strcpy(address.sun_path,path);
    nasd_require(bind(fd,(const struct sockaddr *)&address,sizeof(address))==0,
                 "bind real VFS pathname socket");
    nasd_require(listen(fd,WINDOW_LIMIT)==0,"listen desktop client socket");
    int flags=fcntl(fd,F_GETFL);
    nasd_require(flags>=0 && fcntl(fd,F_SETFL,flags|O_NONBLOCK)==0,
                 "desktop listener nonblocking accepts");
    nasd_require(setenv(NASD_SOCKET_ENV,path,1)==0 &&
                 setenv(NASD_PROTOCOL_ENV,"1",1)==0,
                 "publish desktop discovery environment");
    return fd;
}
static void spawn_client(struct session *s,int role) {
    nasd_require(role>=0 && (unsigned)role<s->count &&
                 (unsigned)role<sizeof(clients)/sizeof(clients[0]),
                 "desktop client registration capacity");
    const char *path=clients[role].path;
    int link[2]={-1,-1};
    nasd_require(socketpair(AF_UNIX,SOCK_STREAM,0,link)==0,
            "create genuine AF_UNIX client IPC");
    pid_t pid=fork();nasd_require(pid>=0,"fork desktop client");
    if(!pid) {
        close(link[0]);
        if(dup2(link[1],3)<0)_exit(122);
        if(dynamic_mode(s->mode) &&
           setenv(NASD_INHERITED_ENV,"3",1)!=0)_exit(120);
        /* Only a single IPC descriptor is inherited by the exec'ed client.
         * Devices, framebuffer and earlier clients remain desktop-owned. */
        for(int i=4;i<64;i++)close(i);
        char *const args[]={(char *)path,
              s->mode==170 || s->mode==171 || dynamic_mode(s->mode) || s->mode==0 ?
                         (role==TERMINAL ? "--m170" :
                         role==BROWSER ? "--m170" : NULL) :
                         (s->mode==168 && role==TERMINAL ? "--m168" : NULL),NULL};
        execv(path,args);
        _exit(121);
    }
    close(link[1]);
    struct window *w=&s->windows[role];
    w->pid=pid;w->channel=link[0];w->role=role;w->owned_child=1;
    w->x=clients[role].x;w->y=clients[role].y;
    unsigned surface_w=0,surface_h=0;
    if(dynamic_mode(s->mode)) {
        int flags=fcntl(w->channel,F_GETFL);
        nasd_require(flags>=0 && fcntl(w->channel,F_SETFL,flags|O_NONBLOCK)==0,
                     "bootstrap client nonblocking surface receive");
        nasd_surface_offer_init(&w->offer);
        uint32_t kind=UINT32_MAX,caps=0;
        int accepted=0;
        for(unsigned tries=0;tries<120 && accepted==0;tries++) {
            struct pollfd ready={.fd=w->channel,.events=POLLIN};
            int n=poll(&ready,1,1000);
            if(n<0 && errno==EINTR)continue;
            nasd_require(n>=0 && !(ready.revents&POLLNVAL),
                         "bootstrap client poll");
            if(n==0)continue;
            accepted=nasd_surface_offer_step(w->channel,&w->offer,&kind,
                          &surface_w,&surface_h,&caps);
        }
        nasd_require(accepted==1 && kind==(uint32_t)role,
                     "bootstrap client valid capability surface");
        int fd=w->offer.surface_fd;w->offer.surface_fd=-1;
        w->capabilities=caps;
        nasd_require(attach_surface(w,fd,surface_w,surface_h)==0,
                     "map bootstrap shared surface");
    } else {
        int fd=nasd_receive_surface(w->channel,role,&surface_w,&surface_h);
        nasd_require(attach_surface(w,fd,surface_w,surface_h)==0,
                     "map legacy shared surface");
    }
    nasd_require(w->pixels[3]==255,"cross-process surface first pixel");
}
static int free_window_slot(const struct session *s) {
    for(unsigned i=0;i<s->count;i++)
        if(!s->windows[i].active && !s->windows[i].pending)return (int)i;
    return -1;
}
static void raise_window(struct session *,unsigned);
static void discard_pending(struct session *s,unsigned slot) {
    struct window *w=&s->windows[slot];
    nasd_require(w->pending && !w->active,"pending registration owner");
    nasd_surface_offer_release(&w->offer);
    close(w->channel);
    *w=(struct window){0};
    ++s->changes;
}
static void accept_external(struct session *s) {
    for(;;) {
        int channel=accept4(s->listener,NULL,NULL,SOCK_NONBLOCK|SOCK_CLOEXEC);
        if(channel<0 && (errno==EAGAIN || errno==EWOULDBLOCK))return;
        if(channel<0 && errno==EINTR)continue;
        nasd_require(channel>=0,"accept independent desktop client");
        const int slot=free_window_slot(s);
        if(slot<0) {close(channel);continue;} /* bounded admission */
        struct window *w=&s->windows[slot];
        *w=(struct window){.channel=channel,.surface_fd=-1,
                            .pid=-1,.role=-1,.pending=1};
        nasd_surface_offer_init(&w->offer);
        ++s->changes;
    }
}
static void complete_external(struct session *s,unsigned slot) {
    struct window *w=&s->windows[slot];
    uint32_t kind=0,capabilities=0;
    unsigned width=0,height=0;
    const int state=nasd_surface_offer_step(w->channel,&w->offer,
                         &kind,&width,&height,&capabilities);
    if(state<0) {discard_pending(s,slot);return;}
    if(!state)return;
    if(kind>UINT16_MAX) {discard_pending(s,slot);return;}
    /* A registration kind is descriptive metadata, not an array index or
     * a uniqueness constraint: two genuine LVGL processes can coexist. */
    const unsigned number=s->external_total;
    const int x=number==0?535:55+(int)((number%4U)*110U);
    const int y=number==0?370:80+(int)((number%4U)*65U);
    w->x=x>SCREEN_W-(int)width?SCREEN_W-(int)width:x;
    w->y=y>SCREEN_H-(int)height-TITLE_H ?
         SCREEN_H-(int)height-TITLE_H:y;
    w->role=(int)kind;
    w->capabilities=capabilities;
    const int fd=w->offer.surface_fd;
    w->offer.surface_fd=-1;
    if(attach_surface(w,fd,width,height)!=0) {
        close(fd);discard_pending(s,slot);return;
    }
    ++s->external_total;++s->external_live;
    s->order[s->live_count++]=slot;
    raise_window(s,slot);
    ++s->changes;
    render(s);
    if(s->stage==0 && kind==LVGL) {
        s->stage=1;
        nasd_mark("M173_EXTERNAL_CLIENT_VISIBLE");
    }
}
static int clamp(int v,int maximum) {
    return v<0?0:v>maximum?maximum:v;
}
static int scale_abs(int value,int min,int max,int pixels) {
    int64_t numerator=(int64_t)(value-min)*(pixels-1);
    return clamp((int)(numerator/(max-min)),pixels-1);
}
static int window_at(const struct session *s,int x,int y) {
    for(unsigned pass=0;pass<s->live_count;pass++) {
        int i=(int)s->order[s->live_count-1-pass];
        const struct window *w=&s->windows[i];
        if(x>=w->x && y>=w->y && x<w->x+w->w &&
           y<w->y+w->h+TITLE_H)return i;
    }
    return -1;
}
static void raise_window(struct session *s,unsigned target) {
    unsigned at=0;
    while(at<s->live_count && s->order[at]!=target)at++;
    nasd_require(at<s->live_count,"raising registered window");
    for(unsigned i=at+1;i<s->live_count;i++)s->order[i-1]=s->order[i];
    s->order[s->live_count-1]=target;
    s->top=(int)target;
    s->focus=(int)target;
}

/* One owner for window retirement. Fixed startup role slots remain stable,
 * while the compact z-order removes dead clients. A crashed child is allowed
 * to die without taking down unrelated desktop processes or hardware FDs. */
static void retire_window(struct session *s,unsigned role,int request_quit) {
    nasd_require(role<s->count && s->windows[role].active,
                 "retire only a registered live window");
    struct window *w=&s->windows[role];
    if(request_quit)nasd_send_packet(w->channel,MSG_QUIT,0);
    int status=0;
    pid_t waited=0;
    if(w->owned_child) {
        do waited=waitpid(w->pid,&status,0);while(waited<0 && errno==EINTR);
        nasd_require(waited==w->pid,"reap exactly one owned desktop child");
    }
    const int abnormal=w->owned_child &&
                       (!WIFEXITED(status) || WEXITSTATUS(status)!=0);
    const size_t size=(size_t)w->w*w->h*4U;
    nasd_require(munmap(w->pixels,size)==0,"release client shared mapping");
    close(w->surface_fd);close(w->channel);
    const int external=!w->owned_child;
    w->surface_fd=w->channel=-1;w->pixels=NULL;w->active=0;
    if(s->pointer_client==(int)role)s->pointer_client=-1;
    unsigned at=0;
    while(at<s->live_count && s->order[at]!=role)at++;
    nasd_require(at<s->live_count,"retired role in z-order");
    for(unsigned i=at+1;i<s->live_count;i++)s->order[i-1]=s->order[i];
    --s->live_count;
    ++s->changes;
    if(external) {
        nasd_require(s->external_live>0,"external client reference balance");
        --s->external_live;
    }
    if(s->focus==(int)role) {
        s->focus=s->live_count?(int)s->order[s->live_count-1]:-1;
        s->top=s->focus;
        s->held=s->dragging=0;
    }
    render(s);
    if((s->mode==167 || s->mode==168) && abnormal)
        nasd_fail("legacy client must exit cleanly");
    if(s->mode==170) {
        if(role==BROWSER && s->stage==0) {
            s->stage=1;nasd_mark("M170_BROWSER_CLOSED_VISIBLE");
        } else if(role==TERMINAL && s->stage==3) {
            s->stage=4;nasd_mark("M170_TERMINAL_CLOSED_VISIBLE");
        }
        if(abnormal)nasd_mark("M170_CRASHED_CLIENT_ISOLATED");
    } else if(s->mode==171 && role==LVGL && s->stage==1) {
        s->stage=2;nasd_mark("M171_LVGL_CLOSED_VISIBLE");
    } else if(dynamic_mode(s->mode) && external && s->stage==2) {
        s->stage=3;nasd_mark("M173_EXTERNAL_CLIENT_CLOSED_VISIBLE");
    }
}

static void pointer_button(struct session *s,int down) {
    /* QEMU may deliver BTN_LEFT through tablet OR mouse. One logical held
     * button is reported to this seat even when devices enumerate twice. */
    if(down==s->held)return;
    s->held=down;
    if(down) {
        int target=window_at(s,s->pointer_x,s->pointer_y);
        if(target<0)return;
        raise_window(s,(unsigned)target);
        struct window *w=&s->windows[target];
        if((s->mode==0 || s->mode==170 || s->mode==171 || dynamic_mode(s->mode)) &&
           s->pointer_y>=w->y && s->pointer_y<w->y+TITLE_H &&
           s->pointer_x>=w->x+w->w-20) {
            retire_window(s,(unsigned)target,1);
            return;
        }
        s->dragging=s->pointer_y>=w->y &&
                    s->pointer_y<w->y+TITLE_H;
        s->grab_x=s->pointer_x;s->grab_y=s->pointer_y;
        s->origin_x=w->x;s->origin_y=w->y;
        render(s);
        if(((s->mode==171 || s->mode==0) && target==LVGL &&
             !s->dragging) ||
           (dynamic_mode(s->mode) && (w->capabilities&NASD_INPUT_POINTER) &&
             !s->dragging)) {
            s->pointer_client=target;
            nasd_send_pointer(w->channel,
                 (unsigned)(s->pointer_x-w->x),
                 (unsigned)(s->pointer_y-w->y-TITLE_H),1);
        }
        if((s->mode==167 || s->mode==168) && s->stage==0 && target==IMAGE && !s->dragging) {
            s->stage=1;nasd_mark(s->mode==168 ?
                 "M168_IMAGE_FOCUS_VISIBLE":"M167_IMAGE_FOCUS_VISIBLE");
        } else if((s->mode==167 || s->mode==168) && s->stage==1 && target==IMAGE && s->dragging) {
            nasd_mark(s->mode==168 ?
                 "M168_IMAGE_DRAG_GRABBED":"M167_IMAGE_DRAG_GRABBED");
        } else if(s->mode==168 && s->stage==2 && target==BROWSER &&
                  !s->dragging) {
            s->stage=3;nasd_mark("M168_BROWSER_FOCUS_VISIBLE");
        } else if(s->mode==168 && s->stage==4 && target==TERMINAL &&
                  !s->dragging) {
            s->stage=5;nasd_mark("M168_TERMINAL_FOCUS_VISIBLE");
        } else if(s->mode==167 && s->stage==2 && target==TERMINAL) {
            s->stage=3;nasd_mark("M167_TERMINAL_FOCUS_VISIBLE");
        }
    } else if(s->pointer_client>=0 &&
               s->windows[s->pointer_client].active &&
               (s->mode==171 || s->mode==0 || dynamic_mode(s->mode))) {
        struct window *w=&s->windows[s->pointer_client];
        nasd_send_pointer(w->channel,
            (unsigned)clamp(s->pointer_x-w->x,w->w-1),
            (unsigned)clamp(s->pointer_y-w->y-TITLE_H,w->h-1),0);
        s->pointer_client=-1;
    } else if(s->dragging) {
        s->dragging=0;
        struct window *w=&s->windows[s->focus];
        if((s->mode==167 || s->mode==168) && s->stage==1 && s->focus==IMAGE &&
           w->x-s->origin_x>=50 && w->y-s->origin_y>=20) {
            render(s);s->stage=2;
            nasd_mark(s->mode==168 ?
                     "M168_IMAGE_MOVED_VISIBLE":"M167_IMAGE_MOVED_VISIBLE");
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
    if(device==0 && e->type==EV_KEY && (e->value==0 || e->value==1)) {
        unsigned bit=0;
        switch(e->code) {
        case KEY_LEFTSHIFT:bit=1U;break;case KEY_RIGHTSHIFT:bit=2U;break;
        case KEY_LEFTCTRL:bit=4U;break;case KEY_RIGHTCTRL:bit=8U;break;
        case KEY_LEFTALT:bit=16U;break;case KEY_RIGHTALT:bit=32U;break;
        default:break;
        }
        if(bit) {
            if(e->value)s->modifier_keys|=bit;
            else s->modifier_keys&=~bit;
            return; /* one combined modifier snapshot travels with next key */
        }
    }
    if(device==0 && e->type==EV_KEY && e->value==1) {
        uint32_t modifiers=0;
        if(s->modifier_keys&3U)modifiers|=NASD_MOD_SHIFT;
        if(s->modifier_keys&12U)modifiers|=NASD_MOD_CTRL;
        if(s->modifier_keys&48U)modifiers|=NASD_MOD_ALT;
        if(dynamic_mode(s->mode)) {
            if(!s->vt_keyboard && e->code==KEY_Q && s->stage==3)
                s->exit_requested=1;
            else if(!s->vt_keyboard && e->code==KEY_ESC)
                s->exit_requested=1;
            else if(s->vt_keyboard && e->code==KEY_F12 &&
                    (modifiers&NASD_MOD_CTRL))s->exit_requested=1;
            else if(s->focus>=0 && s->windows[s->focus].active &&
                    (s->windows[s->focus].capabilities&NASD_INPUT_KEYS))
                nasd_send_key(s->windows[s->focus].channel,e->code,modifiers);
        } else if(s->mode==171 && e->code==KEY_Q && s->stage==2) {
            s->exit_requested=1;
        } else if(s->mode==170 && e->code==KEY_Q && s->stage==4) {
            s->exit_requested=1; /* QMP acceptance finishes after two real commands */
        } else if((s->mode==167 || s->mode==168) &&
                  e->code==KEY_Q && s->stage==(s->mode==168?6:4)) {
            s->stage=s->mode==168?7:5;
            s->exit_requested=1;
        } else if(!s->mode && !s->vt_keyboard && e->code==KEY_ESC) {
            s->exit_requested=1;
        } else if(s->focus>=0 &&
                  s->windows[s->focus].active &&
                  s->focus==BROWSER &&
                  (e->code==KEY_B || (s->mode!=167 && s->mode!=168 &&
                                     e->code==KEY_C))) {
                nasd_send_key(s->windows[BROWSER].channel,e->code,modifiers);
        } else if(s->focus==TERMINAL && s->windows[TERMINAL].active) {
            if(s->mode==170 || s->mode==171 || s->mode==0) {
                /* Translate evdev in the client, not in the compositor.
                 * Each physical key is one asynchronous IPC message. */
                nasd_send_key(s->windows[TERMINAL].channel,
                              e->code,modifiers);
            } else if(e->code==KEY_A &&
                      !(s->sent_key&(1<<TERMINAL))) {
                nasd_send_packet(s->windows[TERMINAL].channel,MSG_KEY,KEY_A);
                s->sent_key|=1<<TERMINAL;
            }
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
static void handle_client_message(struct session *s,unsigned slot,
                                  struct message m) {
    struct window *w=&s->windows[slot];
    const int role=w->role;
    if((s->mode==170 || s->mode==171 || s->mode==0 || dynamic_mode(s->mode)) &&
       m.type==MSG_CLOSE) {
        retire_window(s,slot,0);
        return;
    }
    if(dynamic_mode(s->mode)) {
        /* Only READY advertises capabilities; any registered client can
         * repaint without needing a compositor-side application branch. */
        if(m.type!=MSG_UPDATED) {
            retire_window(s,slot,0);return;
        }
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        render(s);
        if(!w->owned_child && role==LVGL && s->stage==1 &&
           m.key==KEY_ENTER) {
            s->stage=2;nasd_mark("M173_SECOND_LVGL_BUTTON_VISIBLE");
        }
        return;
    }
    nasd_require(m.type==MSG_UPDATED &&
        ((role==LVGL && (s->mode==171 || s->mode==0) &&
          (m.key==0 || m.key==KEY_ENTER)) ||
         (role==BROWSER && m.key==KEY_B) ||
         (role==TERMINAL &&
           ((s->mode==170 || s->mode==171 || s->mode==0) ||
            m.key==KEY_A))),
        "registered client live surface update");
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    render(s);
    if(s->mode==171 && role==LVGL && s->stage==0 &&
       m.key==KEY_ENTER) {
        s->stage=1;nasd_mark("M171_LVGL_BUTTON_CLICK_VISIBLE");
    } else if(s->mode==170 && role==TERMINAL && s->stage==1 &&
              m.key==KEY_1) {
        s->stage=2;nasd_mark("M170_FIRST_COMMAND_VISIBLE");
    } else if(s->mode==170 && role==TERMINAL && s->stage==2 &&
              m.key==KEY_2) {
        s->stage=3;nasd_mark("M170_SECOND_COMMAND_VISIBLE");
    } else if(s->mode==168 && role==BROWSER && s->stage==3) {
        s->stage=4;nasd_mark("M168_BROWSER_KEY_VISIBLE");
    } else if(s->mode==168 && role==TERMINAL && s->stage==5) {
        s->stage=6;nasd_mark("M168_TERMINAL_KEY_VISIBLE");
    } else if(s->mode==167 && role==TERMINAL && s->stage==3) {
        s->stage=4;nasd_mark("M167_TERMINAL_KEY_VISIBLE");
    }
}
static void event_loop(struct session *s) {
    while(!s->exit_requested) {
        const int dynamic=dynamic_mode(s->mode);
        const unsigned base=dynamic?4U:3U;
        struct pollfd fds[4+WINDOW_LIMIT]={
            {.fd=s->input.keyboard,.events=POLLIN},
            {.fd=s->input.tablet,.events=POLLIN},
            {.fd=s->input.mouse,.events=POLLIN}
        };
        if(dynamic)fds[3]=(struct pollfd){.fd=s->listener,.events=POLLIN};
        unsigned roles[WINDOW_LIMIT]={0};
        unsigned watchers=0;
        if(dynamic) {
            for(unsigned i=0;i<s->count;i++) {
                if(!s->windows[i].active && !s->windows[i].pending)continue;
                roles[watchers]=i;
                fds[base+watchers++]=(struct pollfd){
                     .fd=s->windows[i].channel,.events=POLLIN};
            }
        } else {
            for(unsigned i=0;i<s->live_count;i++) {
                roles[watchers]=s->order[i];
                fds[base+watchers++]=(struct pollfd){
                    .fd=s->windows[roles[i]].channel,.events=POLLIN};
            }
        }
        int count=poll(fds,base+watchers,-1);
        if(count<0 && errno==EINTR)continue;
        nasd_require(count>0,"poll hardware devices and live client IPC");
        for(unsigned i=0;i<base+watchers;i++) {
            if(i>=base && !s->windows[roles[i-base]].active &&
               !s->windows[roles[i-base]].pending)continue;
            const short revents=fds[i].revents;
            if(dynamic && i==3) {
                nasd_require(!(revents&POLLNVAL),"listener FD remains valid");
                if(revents&POLLIN) {accept_external(s);break;}
                continue;
            }
            if(i>=base && dynamic) {
                const unsigned slot=roles[i-base];
                struct window *w=&s->windows[slot];
                if(revents&POLLNVAL) {
                    if(w->pending)discard_pending(s,slot);
                    else retire_window(s,slot,0);
                    break;
                }
                /* POLLIN wins over HUP: the last complete MSG_CLOSE may
                 * still be buffered in the real AF_UNIX stream. */
                if(!(revents&POLLIN) && (revents&(POLLHUP|POLLERR))) {
                    if(w->pending)discard_pending(s,slot);
                    else retire_window(s,slot,0);
                    break;
                }
                if(!(revents&POLLIN))continue;
                const unsigned before=s->changes;
                if(w->pending)complete_external(s,slot);
                else {
                    struct message m={0};
                    int state=nasd_packet_step(w->channel,&w->incoming,&m);
                    if(state<0)retire_window(s,slot,0);
                    else if(state==1)handle_client_message(s,slot,m);
                }
                if(s->changes!=before)break;
                continue;
            }
            if(i>=base && (s->mode==170 || s->mode==171 || s->mode==0) &&
               (revents&(POLLHUP|POLLERR|POLLNVAL)) &&
               !(revents&POLLIN)) {
                retire_window(s,roles[i-base],0);
                break; /* rebuild poll entries after z-order compaction */
            }
            /* An AF_UNIX peer can close immediately after writing its
             * final MSG_CLOSE. NaSeed's socket poll may report POLLIN and
             * POLLERR/POLLHUP together for that last buffered message.
             * Read it while POLLIN is set; only treat a non-readable
             * terminal event as an actual disconnected client. */
            if(i<3 || (s->mode!=170 && s->mode!=171 && s->mode!=0))
                nasd_require(!(revents&(POLLERR|POLLNVAL|POLLHUP)),
                             "desktop hardware or client disconnected");
            else
                nasd_require(!(revents&POLLNVAL),
                             "registered client descriptor invalid");
            if(!(revents&POLLIN))continue;
            if(i<3) {
                const unsigned before=s->changes;
                struct input_event events[32];
                ssize_t n=read(fds[i].fd,events,sizeof(events));
                if(n<0 && (errno==EAGAIN || errno==EINTR))continue;
                nasd_require(n>0 && n%(ssize_t)sizeof(events[0])==0,
                        "aligned LP64 hardware event read");
                for(size_t j=0;j<(size_t)n/sizeof(events[0]);j++) {
                    input_event(s,i,&events[j]);
                    if(s->exit_requested || s->changes!=before)break;
                }
                /* A titlebar close or normal quit changes the FDs: return
                 * to poll() before reusing this fixed event snapshot. */
                if(s->exit_requested || s->changes!=before)break;
            } else {
                const unsigned slot=roles[i-base];
                struct message m=nasd_recv_packet(s->windows[slot].channel);
                const unsigned before=s->changes;
                handle_client_message(s,slot,m);
                if(s->changes!=before)break;
            }
        }
    }
}
static void desktop(int mode) {
    struct session s={0};
    const unsigned bootstrap=mode==167?2U:
                             (mode==171 || dynamic_mode(mode) || mode==0?4U:3U);
    s.mode=mode;s.count=dynamic_mode(mode)?WINDOW_LIMIT:bootstrap;
    s.vt_keyboard=mode==187 || (getenv("NASEED_TERMINAL_VT100") &&
             !strcmp(getenv("NASEED_TERMINAL_VT100"),"1"));
    s.pointer_client=-1;s.listener=-1;
    s.input=open_input();
    s.fb=open_fb();
    if(dynamic_mode(mode))s.listener=open_listener();
    for(unsigned i=0;i<bootstrap;i++) {
        spawn_client(&s,(int)i);
        s.order[s.live_count++]=i;
    }
    s.focus=s.top=(int)(bootstrap-1);s.pointer_x=400;s.pointer_y=300;
    render(&s);
    if(mode==187)nasd_mark("M187_DESKTOP_INITIAL_VISIBLE");
    else if(mode==173)nasd_mark("M173_DESKTOP_INITIAL_VISIBLE");
    else if(mode==171)nasd_mark("M171_DESKTOP_INITIAL_VISIBLE");
    else if(mode==170)nasd_mark("M170_DESKTOP_INITIAL_VISIBLE");
    else if(mode)nasd_mark(mode==168?"M168_DESKTOP_INITIAL_VISIBLE":
                                       "M167_DESKTOP_INITIAL_VISIBLE");
    event_loop(&s);
    /* Quit and reap *only* currently owned children; retired roles are
     * already unmapped and reaped, including their unexpected exit status. */
    while(s.live_count) {
        unsigned role=s.order[s.live_count-1];
        retire_window(&s,role,1);
    }
    if(dynamic_mode(mode)) {
        for(unsigned i=0;i<s.count;i++)
            if(s.windows[i].pending)discard_pending(&s,i);
        close(s.listener);
        nasd_require(unlink(getenv(NASD_SOCKET_ENV))==0,
                     "remove owned desktop pathname socket");
    }
    close(s.input.keyboard);close(s.input.tablet);close(s.input.mouse);
    close(s.fb);
    if(dynamic_mode(mode)) {
        if(mode==187) {
            nasd_mark("M187_CLIENTS_REAPED");
            nasd_mark("M187_DESKTOP_PASS");
        } else {
        nasd_mark("M173_CLIENTS_REAPED");
        nasd_mark("M173_DESKTOP_PASS");
        }
    } else if(mode==171) {
        nasd_mark("M171_CLIENTS_REAPED");
        nasd_mark("M171_DESKTOP_PASS");
    } else if(mode==170) {
        nasd_mark("M170_CLIENTS_REAPED");
        nasd_mark("M170_DESKTOP_PASS");
    } else if(mode) {
        nasd_mark(mode==168?"M168_THREE_EXEC_CLIENTS_CLEAN_PASS":
                             "M167_TWO_EXEC_CLIENTS_CLEAN_PASS");
        nasd_mark(mode==168?"M168_DESKTOP_PASS":"M167_DESKTOP_PASS");
    }
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
        nasd_send_surface(ends[1],fd,IMAGE,IMAGE_W,IMAGE_H);
        munmap(pixels,(size_t)IMAGE_W*IMAGE_H*4U);
        close(fd);close(ends[1]);
        _exit(0);
    }
    close(ends[1]);
    unsigned w=0,h=0;
    int fd=nasd_receive_surface(ends[0],IMAGE,&w,&h);
    nasd_require(w==IMAGE_W && h==IMAGE_H,
                 "host client-specified surface metadata identity");
    uint8_t *pixels=mmap(NULL,(size_t)IMAGE_W*IMAGE_H*4U,
                         PROT_READ,MAP_SHARED,fd,0);
    nasd_require(pixels!=MAP_FAILED && pixels[0]==255 && pixels[1]==0 &&
            pixels[2]==255 && pixels[3]==255,
            "cross-process memfd SCM_RIGHTS pixel identity");
    int status=0;
    nasd_require(waitpid(child,&status,0)==child && WIFEXITED(status) &&
            WEXITSTATUS(status)==0,"independent IPC client exit");
    struct session s={.count=2,.live_count=2,.order={TERMINAL,IMAGE}};
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
static void selftest168(void) {
    struct session s={.count=3,.live_count=3,.order={IMAGE,TERMINAL,BROWSER},
                      .focus=BROWSER,.top=BROWSER,.mode=168};
    const int dimensions[3][2]={{IMAGE_W,IMAGE_H},{TERM_W,TERM_H},
                                 {BROWSER_W,BROWSER_H}};
    const uint8_t colors[3][3]={{255,0,255},{20,68,204},{34,95,72}};
    int fds[3]={-1,-1,-1};
    for(unsigned i=0;i<3;i++) {
        struct window *w=&s.windows[i];
        w->x=clients[i].x;w->y=clients[i].y;
        w->w=dimensions[i][0];w->h=dimensions[i][1];
        w->pixels=nasd_make_surface("m168-host-client",w->w,w->h,
                                    &fds[i]);
        nasd_fill_surface(w->pixels,w->w,w->h,
                          colors[i][0],colors[i][1],colors[i][2]);
    }
    for(unsigned k=0;k<3;k++)draw_window(&s.windows[s.order[k]],
                                           (int)s.order[k]==s.focus);
    const size_t overlap=((size_t)210*SCREEN_W+500)*4U;
    nasd_require(screen[overlap+2]==34 && screen[overlap+1]==95,
                 "third client initially owns overlap");
    nasd_require(window_at(&s,600,150)==BROWSER,
                 "third-party client participates in hit-testing");
    raise_window(&s,TERMINAL);
    for(unsigned k=0;k<3;k++)draw_window(&s.windows[s.order[k]],
                                           (int)s.order[k]==s.focus);
    nasd_require(screen[overlap+2]==20 && screen[overlap+1]==68,
                 "terminal raises above dynamically registered browser");
    nasd_draw_text(s.windows[BROWSER].pixels,BROWSER_W,BROWSER_H,
                   8,7,"NASEED",6,255,255,255);
    size_t glyph=((size_t)7*BROWSER_W+8)*4U;
    nasd_require(s.windows[BROWSER].pixels[glyph+2]==255,
                 "real client pixel glyph rasterizer");
    for(unsigned i=0;i<3;i++) {
        struct window *w=&s.windows[i];
        munmap(w->pixels,(size_t)w->w*w->h*4U);
        close(fds[i]);
    }
    nasd_mark("[M168 HOST DESKTOP PASS] three registered dimensioned clients, "
              "z-order, focus, surface pixels and userspace text glyphs");
}

/* M169: exercise the real SCM_RIGHTS receiver, not a mock fstat. Each
 * malformed backing capability is sent over an actual AF_UNIX socket and
 * rejected by a separate receiving process before any mmap/pixel access. */
static void selftest169(void) {
    const off_t proper=(off_t)IMAGE_W*IMAGE_H*4;
    for(unsigned kind=0;kind<4;kind++) {
        int ends[2]={-1,-1};
        nasd_require(socketpair(AF_UNIX,SOCK_STREAM,0,ends)==0,
                     "M169 surface test socketpair");
        int backing=-1;
        if(kind==2) {
            int pipefds[2]={-1,-1};
            nasd_require(pipe(pipefds)==0,"M169 nonregular backing pipe");
            backing=pipefds[0];
            close(pipefds[1]);
        } else if(kind!=3) {
            backing=memfd_create("m169-invalid-surface",MFD_CLOEXEC);
            nasd_require(backing>=0,"M169 real memfd backing");
            off_t length=kind==0?proper-1:proper+1;
            nasd_require(ftruncate(backing,length)==0,
                         "M169 malformed backing size");
        }
        if(kind==3) {
            const struct message packet={
                .type=MSG_READY,.key=IMAGE,
                .value=(IMAGE_W<<16U)|IMAGE_H
            };
            nasd_write_all(ends[0],&packet,sizeof(packet));
        } else {
            nasd_send_surface(ends[0],backing,IMAGE,IMAGE_W,IMAGE_H);
        }
        pid_t receiver=fork();
        nasd_require(receiver>=0,"M169 invalid surface receiver fork");
        if(!receiver) {
            close(ends[0]);
            if(backing>=0)close(backing);
            FILE *quiet=freopen("/dev/null","w",stderr);
            (void)quiet;
            unsigned w=0,h=0;
            int fd=nasd_receive_surface(ends[1],IMAGE,&w,&h);
            (void)fd;
            _exit(99); /* accepted an invalid FD: forbidden */
        }
        close(ends[0]);close(ends[1]);
        if(backing>=0)close(backing);
        int status=0;
        nasd_require(waitpid(receiver,&status,0)==receiver &&
                    WIFEXITED(status) && WEXITSTATUS(status)==17,
                    "M169 reject malformed SCM_RIGHTS backing before mmap");
    }
    /* Without QMP stages, focused keys still reach the correct userspace
     * client over real IPC. Browser accepts repeated B; the one-shot PTY
     * fixture is dispatched only once after its shell exits. */
    int browser[2],terminal[2];
    nasd_require(socketpair(AF_UNIX,SOCK_STREAM,0,browser)==0 &&
                 socketpair(AF_UNIX,SOCK_STREAM,0,terminal)==0,
                 "M169 focused keyboard channels");
    struct session s={.mode=168,.count=3,.focus=BROWSER,.stage=0};
    s.windows[BROWSER].channel=browser[0];
    s.windows[TERMINAL].channel=terminal[0];
    s.windows[BROWSER].active=1;
    s.windows[TERMINAL].active=1;
    const struct input_event b={.type=EV_KEY,.code=KEY_B,.value=1};
    const struct input_event a={.type=EV_KEY,.code=KEY_A,.value=1};
    for(unsigned n=0;n<2;n++) {
        input_event(&s,0,&b);
        struct message packet=nasd_recv_packet(browser[1]);
        nasd_require(packet.type==MSG_KEY && packet.key==KEY_B,
                     "M169 repeated browser key without acceptance stage");
    }
    s.focus=TERMINAL;
    input_event(&s,0,&a);
    struct message packet=nasd_recv_packet(terminal[1]);
    nasd_require(packet.type==MSG_KEY && packet.key==KEY_A,
                 "M169 focused terminal key without acceptance stage");
    input_event(&s,0,&a);
    nasd_require((s.sent_key&(1<<TERMINAL))!=0,
                 "M169 one-shot shell not sent a second input");
    close(browser[0]);close(browser[1]);
    close(terminal[0]);close(terminal[1]);
    nasd_mark("[M169 HOST DESKTOP PASS] invalid SCM_RIGHTS size/type/missing FD rejected, unstaged focused keys");
}


static void selftest170(void) {
    /* Crash isolation with the production retirement path, real reaping,
     * real mapped shared surfaces and a real positional framebuffer FD. */
    struct session s={.count=3,.live_count=3,.mode=0,
                      .focus=BROWSER,.top=BROWSER,
                      .order={IMAGE,TERMINAL,BROWSER}};
    s.fb=memfd_create("m170-host-display",MFD_CLOEXEC);
    nasd_require(s.fb>=0 && ftruncate(s.fb,sizeof(screen))==0,
                 "M170 real host framebuffer backing");
    int surface_fds[3]={-1,-1,-1};
    const int widths[]={IMAGE_W,TERM_W,BROWSER_W};
    const int heights[]={IMAGE_H,TERM_H,BROWSER_H};
    for(unsigned i=0;i<3;i++) {
        struct window *w=&s.windows[i];
        w->x=clients[i].x;w->y=clients[i].y;
        w->w=widths[i];w->h=heights[i];w->role=(int)i;
        w->active=1;
        w->pixels=nasd_make_surface("m170-host-window",w->w,w->h,
                                    &surface_fds[i]);
        w->surface_fd=surface_fds[i];
        nasd_fill_surface(w->pixels,w->w,w->h,
                          i==BROWSER?34:20,i==BROWSER?95:68,
                          i==BROWSER?72:204);
    }
    int channel[2];
    nasd_require(socketpair(AF_UNIX,SOCK_STREAM,0,channel)==0,
                 "M170 crash test client IPC");
    pid_t child=fork();
    nasd_require(child>=0,"M170 crash isolation fork");
    if(!child) {
        close(channel[0]);
        _exit(23); /* one defective app; compositor must remain alive */
    }
    close(channel[1]);
    s.windows[BROWSER].pid=child;
    s.windows[BROWSER].channel=channel[0];
    s.windows[BROWSER].owned_child=1;
    retire_window(&s,BROWSER,0);
    nasd_require(s.live_count==2 && s.focus==TERMINAL &&
                 s.order[0]==IMAGE && s.order[1]==TERMINAL &&
                 !s.windows[BROWSER].active && s.windows[BROWSER].pixels==NULL,
                 "M170 crash removes exactly one z-order entry and focus");
    uint8_t pixel[4]={0};
    const off_t offset=((off_t)250*SCREEN_W+500)*4;
    nasd_require(pread(s.fb,pixel,sizeof(pixel),offset)==4 &&
                 pixel[0]==204 && pixel[1]==68 && pixel[2]==20,
                 "M170 real framebuffer reveals preserved terminal");
    for(unsigned i=0;i<2;i++) {
        struct window *w=&s.windows[i];
        munmap(w->pixels,(size_t)w->w*w->h*4U);
        close(w->surface_fd);
    }
    close(s.fb);
    nasd_mark("[M170 HOST DESKTOP PASS] crash reaped, stable z-order, focus and physical backing cleanup");
}
static void selftest187(void) {
    int channels[2];
    nasd_require(socketpair(AF_UNIX,SOCK_STREAM,0,channels)==0,
                 "M187 real compositor-to-VT IPC");
    struct session s={.mode=187,.count=WINDOW_LIMIT,.focus=TERMINAL,
                      .vt_keyboard=1};
    s.windows[TERMINAL].active=1;
    s.windows[TERMINAL].channel=channels[0];
    s.windows[TERMINAL].capabilities=NASD_INPUT_KEYS;
    struct input_event e={.type=EV_KEY,.code=KEY_LEFTCTRL,.value=1};
    input_event(&s,0,&e);
    e.code=KEY_X;input_event(&s,0,&e);
    struct message key=nasd_recv_packet(channels[1]);
    nasd_require(key.type==MSG_KEY && key.key==KEY_X &&
                 key.value==NASD_MOD_CTRL,"Ctrl+X modifier over actual IPC");
    e.code=KEY_UP;input_event(&s,0,&e);
    key=nasd_recv_packet(channels[1]);
    nasd_require(key.key==KEY_UP && key.value==NASD_MOD_CTRL,
                 "physical VT arrow key preserved over IPC");
    e.code=KEY_LEFTCTRL;e.value=0;input_event(&s,0,&e);
    e.code=KEY_ESC;e.value=1;input_event(&s,0,&e);
    key=nasd_recv_packet(channels[1]);
    nasd_require(key.key==KEY_ESC && key.value==0 && !s.exit_requested,
                 "M187 ESC reaches focused PTY instead of quitting desktop");
    e.code=KEY_RIGHTSHIFT;input_event(&s,0,&e);
    e.code=KEY_A;input_event(&s,0,&e);
    key=nasd_recv_packet(channels[1]);
    nasd_require(key.key==KEY_A && key.value==NASD_MOD_SHIFT,
                 "hardware Shift preserved to client");
    e.code=KEY_RIGHTSHIFT;e.value=0;input_event(&s,0,&e);
    e.code=KEY_LEFTCTRL;e.value=1;input_event(&s,0,&e);
    e.code=KEY_F12;input_event(&s,0,&e);
    nasd_require(s.exit_requested,"Ctrl+F12 deliberate M187 desktop exit");
    struct session legacy={.mode=173};
    e.code=KEY_ESC;input_event(&legacy,0,&e);
    nasd_require(legacy.exit_requested,"M173 historical ESC exit unchanged");
    close(channels[0]);close(channels[1]);
    nasd_mark("[M187 HOST COMPOSITOR PASS] real IPC modifiers, ESC key routing, "
              "Ctrl+F12 exit and M173 compatibility");
}

/* Exercise the production pathname listener and admission/retirement path
 * without a QEMU screen. The two clients deliberately register the SAME
 * application kind; their connection identities must never alias. */
static void selftest173_client(int listener,unsigned color) {
    close(listener);
    (void)unsetenv(NASD_INHERITED_ENV);
    int channel=nasd_client_channel();
    int fd=-1;
    const unsigned width=40,height=40;
    uint8_t *pixels=nasd_make_surface("m173-host-external",width,height,&fd);
    nasd_fill_surface(pixels,width,height,(uint8_t)color,
                      (uint8_t)(255U-color),80);
    nasd_send_surface_caps(channel,fd,LVGL,width,height,
                           NASD_INPUT_POINTER);
    struct message m=nasd_recv_packet(channel);
    nasd_require(m.type==MSG_QUIT,"external host client shutdown");
    munmap(pixels,(size_t)width*height*4U);
    close(fd);close(channel);
    _exit(0);
}
static void selftest173_protocol(void) {
    nasd_require(setenv(NASD_PROTOCOL_ENV,"1",1)==0,
                 "M173 protocol test version");
    int ends[2]={-1,-1};
    nasd_require(socketpair(AF_UNIX,SOCK_STREAM,0,ends)==0,
                 "M173 real nonblocking protocol channel");
    int flags=fcntl(ends[1],F_GETFL);
    nasd_require(flags>=0 && fcntl(ends[1],F_SETFL,flags|O_NONBLOCK)==0,
                 "M173 receive socket must not park compositor");
    int fd=memfd_create("m173-protocol",MFD_CLOEXEC);
    nasd_require(fd>=0 && ftruncate(fd,40*40*4)==0,
                 "M173 physical memfd descriptor");
    nasd_send_surface_caps(ends[0],fd,LVGL,40,40,NASD_INPUT_POINTER);
    struct nasd_surface_offer offer;
    nasd_surface_offer_init(&offer);
    uint32_t kind=0,capabilities=0;
    unsigned width=0,height=0;
    nasd_require(nasd_surface_offer_step(ends[1],&offer,&kind,&width,
                  &height,&capabilities)==1 && kind==LVGL &&
                 capabilities==NASD_INPUT_POINTER && width==40 && height==40,
                 "M173 incremental real SCM_RIGHTS capability offer");
    nasd_surface_offer_release(&offer);
    close(fd);
    const struct message updated={.type=MSG_UPDATED,.key=KEY_ENTER};
    nasd_write_all(ends[0],&updated,5);
    struct nasd_packet_reader reader={0};
    struct message received={0};
    nasd_require(nasd_packet_step(ends[1],&reader,&received)==0 &&
                 reader.received==5,
                 "M173 partial packet must not block independent clients");
    nasd_write_all(ends[0],(const uint8_t *)&updated+5,
                   sizeof(updated)-5);
    nasd_require(nasd_packet_step(ends[1],&reader,&received)==1 &&
                 received.type==MSG_UPDATED && received.key==KEY_ENTER,
                 "M173 packet assembly after independent writable event");
    close(ends[0]);close(ends[1]);
    nasd_require(socketpair(AF_UNIX,SOCK_STREAM,0,ends)==0,
                 "M173 malformed peer real IPC");
    nasd_surface_offer_init(&offer);
    const struct message invalid={.type=MSG_READY,.key=LVGL,
                                  .value=(40U<<16U)|40U};
    nasd_write_all(ends[0],&invalid,sizeof(invalid));
    nasd_require(nasd_surface_offer_step(ends[1],&offer,NULL,NULL,
                                        NULL,NULL)<0 && offer.surface_fd<0,
                 "M173 reject missing SCM_RIGHTS without compositor exit");
    nasd_surface_offer_release(&offer);
    close(ends[0]);close(ends[1]);
    nasd_mark("[M173 HOST PROTOCOL PASS] nonblocking validated SCM_RIGHTS, "
              "partial packets and isolated invalid clients");
}
static void selftest173(void) {
    selftest173_protocol();
    char directory[]="/tmp/naseed-m173-host-XXXXXX";
    nasd_require(mkdtemp(directory)!=NULL,"host unique socket directory");
    char path[108];
    nasd_require(snprintf(path,sizeof(path),"%s/session.sock",directory)>0 &&
                 strlen(path)<sizeof(((struct sockaddr_un *)0)->sun_path),
                 "host pathname socket length");
    nasd_require(setenv(NASD_SOCKET_ENV,path,1)==0,
                 "host unique desktop socket selection");
    struct session s={.mode=173,.count=WINDOW_LIMIT,.focus=-1,.top=-1,
                      .pointer_client=-1};
    s.fb=memfd_create("m173-host-physical-screen",MFD_CLOEXEC);
    nasd_require(s.fb>=0 && ftruncate(s.fb,sizeof(screen))==0,
                 "host framebuffer byte-owner fixture");
    s.listener=open_listener();

    /* One unrelated, invalid peer must not kill the compositor, leak an
     * untrusted FD or consume a permanent window slot. */
    int bad=socket(AF_UNIX,SOCK_STREAM,0);
    nasd_require(bad>=0,"malformed peer real socket");
    struct sockaddr_un address={.sun_family=AF_UNIX};
    strcpy(address.sun_path,path);
    nasd_require(connect(bad,(struct sockaddr *)&address,sizeof(address))==0,
                 "malformed peer real connect");
    const struct message bad_packet={.type=MSG_READY,.key=LVGL,
                                     .value=(40U<<16U)|40U};
    nasd_write_all(bad,&bad_packet,sizeof(bad_packet)); /* no SCM_RIGHTS */
    accept_external(&s);
    nasd_require(s.windows[0].pending,"malformed peer pending admission");
    complete_external(&s,0);
    nasd_require(!s.windows[0].pending && !s.windows[0].active &&
                 s.live_count==0,"malformed surface isolated and released");
    close(bad);

    pid_t children[2]={-1,-1};
    for(unsigned i=0;i<2;i++) {
        children[i]=fork();
        nasd_require(children[i]>=0,"two independently forked GUI clients");
        if(children[i]==0)selftest173_client(s.listener,30U+i*150U);
    }
    int slots[2]={-1,-1};
    for(unsigned tries=0;tries<150 && s.live_count<2;tries++) {
        struct pollfd listener={.fd=s.listener,.events=POLLIN};
        (void)poll(&listener,1,30);
        if(listener.revents&POLLIN)accept_external(&s);
        for(unsigned slot=0;slot<s.count;slot++) {
            if(!s.windows[slot].pending)continue;
            struct pollfd ready={.fd=s.windows[slot].channel,.events=POLLIN};
            (void)poll(&ready,1,20);
            if(ready.revents&POLLIN)complete_external(&s,slot);
        }
    }
    nasd_require(s.live_count==2 && s.external_live==2,
                 "two dynamic GUI registrations accepted independently");
    slots[0]=(int)s.order[0];slots[1]=(int)s.order[1];
    nasd_require(slots[0]!=slots[1] && s.windows[slots[0]].role==LVGL &&
                 s.windows[slots[1]].role==LVGL &&
                 s.windows[slots[0]].capabilities==NASD_INPUT_POINTER &&
                 s.windows[slots[1]].capabilities==NASD_INPUT_POINTER,
                 "same app kind has independent slots and capabilities");
    uint8_t first[4]={0},second[4]={0};
    const struct window *a=&s.windows[slots[0]],*b=&s.windows[slots[1]];
    off_t one=((off_t)(a->y+TITLE_H)*SCREEN_W+a->x)*4;
    off_t two=((off_t)(b->y+TITLE_H)*SCREEN_W+b->x)*4;
    nasd_require(pread(s.fb,first,4,one)==4 &&
                 pread(s.fb,second,4,two)==4 &&
                 first[3]==255 && second[3]==255 &&
                 first[2]!=second[2],
                 "independent original memfd pixel surfaces in one display");
    retire_window(&s,(unsigned)slots[1],1);
    nasd_require(s.live_count==1 && s.windows[slots[0]].active &&
                 !s.windows[slots[1]].active && s.external_live==1,
                 "close one nonchild GUI without retiring its sibling");
    retire_window(&s,(unsigned)slots[0],1);
    nasd_require(!s.live_count && !s.external_live && s.focus==-1,
                 "all dynamic surface/map/FD and focus owners released");
    for(unsigned i=0;i<2;i++) {
        int status=0;
        nasd_require(waitpid(children[i],&status,0)==children[i] &&
                     WIFEXITED(status) && WEXITSTATUS(status)==0,
                     "compositor did not reap external application child");
    }
    close(s.listener);close(s.fb);
    nasd_require(unlink(path)==0 && rmdir(directory)==0,
                 "host dynamic listener pathname cleanup");
    nasd_mark("[M173 HOST DYNAMIC PASS] real pathname listen/connect, "
         "malformed peer isolation, duplicate-kind SCM_RIGHTS windows, "
         "independent pixels, nonchild close and focused resource release");
}

int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--selftest")) {
        selftest();return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--selftest168")) {
        selftest();selftest168();return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--selftest169")) {
        selftest();selftest168();selftest169();return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--selftest170")) {
        selftest();selftest168();selftest169();selftest170();return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--selftest187")) {
        selftest187();return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--selftest173")) {
        selftest173();return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--selftest173proto")) {
        selftest173_protocol();return 0;
    }
    /* No selector runs a persistent desktop. M170 uses new PTY + lifecycle
     * gates, earlier selectors still run their exact old QMP workflows. */
    nasd_require(argc==1 || (argc==2 &&
                 (!strcmp(argv[1],"m167") || !strcmp(argv[1],"m168") ||
                  !strcmp(argv[1],"m170") || !strcmp(argv[1],"m171") ||
                  !strcmp(argv[1],"m173") || !strcmp(argv[1],"--m173") ||
                  !strcmp(argv[1],"m187"))),
                 "desktop mode arguments");
    desktop(argc==1?173:(!strcmp(argv[1],"m168")?168:
                       !strcmp(argv[1],"m170")?170:
                       !strcmp(argv[1],"m171")?171:
                       !strcmp(argv[1],"m187")?187:
                       (!strcmp(argv[1],"m173") ||
                        !strcmp(argv[1],"--m173"))?173:167));
    return 0;
}
