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
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "lvgl.h"

/* This is an ACTUAL upstream LVGL 9.4 client, not hand-painted demo glyphs.
 * The desktop and kernel never see LVGL objects. The client alone owns the
 * LVGL input device, display and XRGB8888 anonymous shared memory. */
enum { GUI_W=240, GUI_H=176, GUI_DRAW_ROWS=44 };
struct lvgl_app {
    int channel,surface_fd,flushed,clicks;
    int reported_clicks;
    unsigned slider_changes,timer_updates,scroll_events;
    int reported_drag,reported_scroll,reported_timer;
    uint8_t *surface;
    uint8_t draw[GUI_W*GUI_DRAW_ROWS*4U];
    int pointer_x,pointer_y,pointer_down;
    uint64_t started_ms;
    lv_display_t *display;
    lv_indev_t *indev;
    lv_obj_t *button,*status,*slider,*slider_status,*clock,*scroll_list;
};
static struct lvgl_app app={.channel=-1,.surface_fd=-1};

static uint64_t monotonic_ms(void) {
    struct timespec now={0};
    nasd_require(clock_gettime(CLOCK_MONOTONIC,&now)==0,
                 "native monotonic GUI clock");
    return (uint64_t)now.tv_sec*1000U+(uint64_t)now.tv_nsec/1000000U;
}
/* Official LVGL timers are driven by kernel CLOCK_MONOTONIC, not artificial
 * time increments for each mouse event. This also works with no input. */
static uint32_t lvgl_tick_ms(void) { return (uint32_t)monotonic_ms(); }

static void flush_surface(lv_display_t *display,const lv_area_t *area,
                          uint8_t *pixels) {
    nasd_require(area->x1>=0 && area->y1>=0 &&
                 area->x2<GUI_W && area->y2<GUI_H &&
                 area->x1<=area->x2 && area->y1<=area->y2,
                 "LVGL flush clipped to registered memfd");
    const unsigned w=(unsigned)(area->x2-area->x1+1);
    const unsigned h=(unsigned)(area->y2-area->y1+1);
    for(unsigned row=0;row<h;row++) {
        uint8_t *dst=app.surface+
             ((size_t)(area->y1+(int)row)*GUI_W+(unsigned)area->x1)*4U;
        const uint8_t *src=pixels+(size_t)row*w*4U;
        for(unsigned x=0;x<w;x++) {
            dst[4*x+0]=src[4*x+0];
            dst[4*x+1]=src[4*x+1];
            dst[4*x+2]=src[4*x+2];
            dst[4*x+3]=255; /* XRGB opaque compositor contract */
        }
    }
    app.flushed=1;
    lv_display_flush_ready(display);
}
static void pointer_read(lv_indev_t *indev,lv_indev_data_t *data) {
    (void)indev;
    data->point.x=(int16_t)app.pointer_x;
    data->point.y=(int16_t)app.pointer_y;
    data->state=app.pointer_down?LV_INDEV_STATE_PRESSED:
                                LV_INDEV_STATE_RELEASED;
}
static void button_clicked(lv_event_t *event) {
    (void)event;
    app.clicks++;
    lv_label_set_text_fmt(app.status,"CLICKS: %d",app.clicks);
    lv_obj_set_style_bg_color(app.button,lv_color_hex(0xe64b40),0);
}
static void slider_changed(lv_event_t *event) {
    (void)event;
    ++app.slider_changes;
    lv_label_set_text_fmt(app.slider_status,"LEVEL: %d",
                          (int)lv_slider_get_value(app.slider));
}
static void clock_advanced(lv_timer_t *timer) {
    (void)timer;
    const uint64_t elapsed=monotonic_ms()-app.started_ms;
    ++app.timer_updates;
    lv_label_set_text_fmt(app.clock,"UP: %u.%us",
                          (unsigned)(elapsed/1000U),
                          (unsigned)((elapsed%1000U)/100U));
    if(app.channel>=0 && !app.reported_timer &&
       app.timer_updates>=2 && elapsed>=200U) {
        app.reported_timer=1;
        nasd_mark("M188_LVGL_TIMER_PASS");
    }
}
static void pump_lvgl(void) {
    (void)lv_timer_handler();
}
/* LVGL can dispatch CLICKED before its next display refresh timer fires.
 * Preserve the input acknowledgement until the *pixels* have been flushed:
 * otherwise a click on one poll iteration and a repaint on the next sends
 * MSG_UPDATED(key=0), losing M173's real physical-button evidence. */
static int flushed_update_key(void) {
    if(!app.flushed)return -1;
    const int clicked=app.clicks>app.reported_clicks;
    app.reported_clicks=app.clicks;
    app.flushed=0;
    return clicked?KEY_ENTER:0;
}
static void publish_flushed_update(void) {
    const int key=flushed_update_key();
    if(key<0)return;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    nasd_send_packet(app.channel,MSG_UPDATED,(uint32_t)key);
}
static void init_lvgl_surface(void) {
    app.surface=nasd_make_surface("m171-real-lvgl",GUI_W,GUI_H,
                                   &app.surface_fd);
    nasd_fill_surface(app.surface,GUI_W,GUI_H,20,32,50);
    lv_init();
    app.started_ms=monotonic_ms();
    lv_tick_set_cb(lvgl_tick_ms);
    app.display=lv_display_create(GUI_W,GUI_H);
    nasd_require(app.display!=NULL,"upstream LVGL display create");
    lv_display_set_color_format(app.display,LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(app.display,app.draw,NULL,sizeof(app.draw),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(app.display,flush_surface);
    app.indev=lv_indev_create();
    nasd_require(app.indev!=NULL,"upstream LVGL pointer indev create");
    lv_indev_set_type(app.indev,LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(app.indev,app.display);
    lv_indev_set_read_cb(app.indev,pointer_read);
    /* IPC is already event-driven. Read every physical motion/press/release
     * immediately so short clicks and continuous drags cannot be missed. */
    lv_indev_set_mode(app.indev,LV_INDEV_MODE_EVENT);

    lv_obj_t *screen=lv_screen_active();
    lv_obj_set_style_bg_color(screen,lv_color_hex(0x14243a),0);
    lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
    lv_obj_t *heading=lv_label_create(screen);
    lv_label_set_text(heading,"REAL LVGL 9.4");
    lv_obj_set_style_text_color(heading,lv_color_hex(0xf0f8ff),0);
    lv_obj_align(heading,LV_ALIGN_TOP_MID,0,9);
    app.clock=lv_label_create(screen);
    lv_label_set_text(app.clock,"UP: 0.0s");
    lv_obj_set_style_text_color(app.clock,lv_color_hex(0xb7d5ff),0);
    lv_obj_set_pos(app.clock,14,31);
    nasd_require(lv_timer_create(clock_advanced,100,NULL)!=NULL,
                 "upstream LVGL time-based timer");
    app.button=lv_button_create(screen);
    lv_obj_set_size(app.button,132,52);
    lv_obj_set_pos(app.button,12,56);
    lv_obj_set_style_bg_color(app.button,lv_color_hex(0x1668a0),0);
    lv_obj_add_event_cb(app.button,button_clicked,LV_EVENT_CLICKED,NULL);
    lv_obj_t *button_text=lv_label_create(app.button);
    lv_label_set_text(button_text,"CLICK HERE");
    lv_obj_center(button_text);
    app.status=lv_label_create(screen);
    lv_label_set_text(app.status,"CLICKS: 0");
    lv_obj_set_style_text_color(app.status,lv_color_hex(0xf5e4b4),0);
    lv_obj_set_pos(app.status,16,114);

    app.slider=lv_slider_create(screen);
    lv_obj_set_size(app.slider,122,14);
    lv_obj_set_pos(app.slider,18,139);
    lv_slider_set_range(app.slider,0,100);
    lv_slider_set_value(app.slider,20,LV_ANIM_OFF);
    lv_obj_add_event_cb(app.slider,slider_changed,LV_EVENT_VALUE_CHANGED,NULL);
    app.slider_status=lv_label_create(screen);
    lv_label_set_text(app.slider_status,"LEVEL: 20");
    lv_obj_set_style_text_color(app.slider_status,lv_color_hex(0xbed8db),0);
    lv_obj_set_pos(app.slider_status,16,157);

    /* A genuine LVGL scrollable container, deliberately taller than its
     * viewport. Both pointer dragging and physical wheel IPC can scroll it. */
    app.scroll_list=lv_obj_create(screen);
    lv_obj_set_size(app.scroll_list,79,111);
    lv_obj_set_pos(app.scroll_list,153,41);
    lv_obj_add_flag(app.scroll_list,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(app.scroll_list,LV_DIR_VER);
    lv_obj_set_scrollbar_mode(app.scroll_list,LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_pad_all(app.scroll_list,3,0);
    static const char *const entries[]={
        "FILES","EXT4","NET","IRQ","GPU","INPUT",
        "SHELL","PTY","LVGL","SCROLL","READY"
    };
    for(unsigned i=0;i<sizeof(entries)/sizeof(entries[0]);i++) {
        lv_obj_t *row=lv_label_create(app.scroll_list);
        lv_label_set_text(row,entries[i]);
        lv_obj_set_pos(row,1,(int)i*20);
    }
    lv_obj_update_layout(screen);
    lv_refr_now(app.display);
    nasd_require(app.flushed && app.surface[3]==255,
                 "real LVGL initial pixels reached shared surface");
    app.flushed=0;
}
static void set_pointer(unsigned x,unsigned y,int down) {
    nasd_require(x<GUI_W && y<GUI_H &&
                 (down==0 || down==1),"LVGL client pointer coordinates");
    const int dragging=down && app.pointer_down &&
                       ((int)x!=app.pointer_x || (int)y!=app.pointer_y);
    const int slider_before=lv_slider_get_value(app.slider);
    app.pointer_x=(int)x;app.pointer_y=(int)y;
    app.pointer_down=down;
    lv_indev_read(app.indev);
    pump_lvgl();
    if(app.channel>=0 && !app.reported_drag && dragging &&
       lv_slider_get_value(app.slider)!=slider_before &&
       app.slider_changes) {
        app.reported_drag=1;
        nasd_mark("M188_LVGL_SLIDER_DRAG_PASS");
    }
}
static void scroll_panel(int32_t vertical,int32_t horizontal) {
    /* Wheel units are signed 120ths of a detent. Clamp untrusted IPC so the
     * conversion cannot overflow, while preserving high-resolution steps. */
    if(vertical>1200)vertical=1200;
    if(vertical< -1200)vertical= -1200;
    if(horizontal>1200)horizontal=1200;
    if(horizontal< -1200)horizontal= -1200;
    const int before=lv_obj_get_scroll_y(app.scroll_list);
    lv_obj_scroll_by_bounded(app.scroll_list,horizontal/5,vertical/5,
                             LV_ANIM_OFF);
    pump_lvgl();
    if(lv_obj_get_scroll_y(app.scroll_list)!=before) {
        ++app.scroll_events;
        if(app.channel>=0 && !app.reported_scroll) {
            app.reported_scroll=1;
            nasd_mark("M188_LVGL_SCROLL_PASS");
        }
    }
}
static uint64_t surface_sum(void) {
    uint64_t hash=1469598103934665603ULL;
    for(size_t n=0;n<(size_t)GUI_W*GUI_H*4U;n++) {
        hash^=app.surface[n];
        hash*=1099511628211ULL;
    }
    return hash;
}
static void cleanup(void) {
    lv_deinit();
    nasd_require(munmap(app.surface,(size_t)GUI_W*GUI_H*4U)==0,
                 "unmap LVGL client surface");
    close(app.surface_fd);
}
static void host_selftest(void) {
    init_lvgl_surface();
    uint64_t before=surface_sum();
    set_pointer(120,96,1);
    set_pointer(120,96,0);
    lv_refr_now(app.display);
    nasd_require(app.clicks==1 && app.flushed &&
                 surface_sum()!=before,
                 "original LVGL pointer event repainted real XRGB pixels");
    /* A deferred real-clock refresh must not erase the CLICKED notification.
     * This isolates the cross-poll state machine from the refresh cadence. */
    app.flushed=0;
    nasd_require(flushed_update_key()==-1 && app.reported_clicks==0,
                 "LVGL click waits for physical framebuffer flush");
    app.flushed=1;
    nasd_require(flushed_update_key()==KEY_ENTER && app.reported_clicks==1,
                 "LVGL delayed refresh acknowledges the real click once");
    nasd_require(flushed_update_key()==-1,
                 "LVGL notification has exactly one physical flush owner");
    const int slider_before=lv_slider_get_value(app.slider);
    set_pointer(28,146,1);
    set_pointer(126,146,1);
    set_pointer(126,146,0);
    nasd_require(lv_slider_get_value(app.slider)>slider_before &&
                 app.slider_changes>0,
                 "real LVGL continuous pointer drag changes slider value");
    scroll_panel(-120,0);
    nasd_require(app.scroll_events>0,
                 "original LVGL scroll container responds to signed wheel");
    const unsigned ticks=app.timer_updates;
    struct timespec pause={.tv_sec=0,.tv_nsec=240000000};
    while(nanosleep(&pause,&pause)<0 && errno==EINTR){}
    pump_lvgl();
    nasd_require(app.timer_updates>ticks &&
                 monotonic_ms()-app.started_ms>=200U,
                 "real CLOCK_MONOTONIC drives LVGL with no pointer events");
    cleanup();
    nasd_mark("[M171 HOST LVGL PASS] pinned upstream LVGL linked, "
              "pointer button click and real XRGB memfd flush; "
              "M188 deferred click acknowledgement, monotonic timer, "
              "slider drag and wheel scroll PASS");
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--selftest171")) {
        host_selftest();return 0;
    }
    nasd_require(argc==1,"LVGL client arguments");
    app.channel=nasd_client_channel();
    init_lvgl_surface();
    __atomic_thread_fence(__ATOMIC_RELEASE);
    nasd_send_surface_caps(app.channel,app.surface_fd,LVGL,GUI_W,GUI_H,
                           NASD_INPUT_POINTER|NASD_INPUT_SCROLL|
                           NASD_INPUT_POINTER_MOTION);
    nasd_mark("M171_LVGL_REAL_RENDER_PASS");
    for(;;) {
        /* The LVGL timer handler owns all idle-time UI updates; the OS poll
         * deadline is derived from real upstream timer readiness. */
        unsigned wait_ms=lv_timer_handler();
        if(wait_ms>100U)wait_ms=100U;
        if(wait_ms<1U)wait_ms=1U;
        publish_flushed_update();
        struct pollfd in={.fd=app.channel,.events=POLLIN};
        int ready=poll(&in,1,(int)wait_ms);
        if(ready<0 && errno==EINTR)continue;
        nasd_require(ready>=0,"LVGL event/timer poll");
        if(!ready)continue;
        nasd_require(in.revents&POLLIN,"LVGL compositor channel alive");
        struct message m=nasd_recv_packet(app.channel);
        if(m.type==MSG_QUIT)break;
        const int before=app.clicks;
        if(m.type==MSG_POINTER) {
            nasd_require(m.key<=1,"LVGL pointer press state");
            const unsigned x=m.value>>16U,y=m.value&65535U;
            set_pointer(x,y,(int)m.key);
        }else if(m.type==MSG_POINTER_LEAVE) {
            /* A released pointer must not leave a slider drag latched when
             * the mouse moves outside this independent application window;
             * release outside widgets to avoid inventing a click. */
            set_pointer(0,0,0);
        }else if(m.type==MSG_SCROLL) {
            scroll_panel((int32_t)m.key,(int32_t)m.value);
        }else nasd_require(0,"LVGL client input IPC type");
        publish_flushed_update();
        if(app.clicks>before && app.clicks==1)
            nasd_mark("M171_LVGL_REAL_BUTTON_CLICK_PASS");
    }
    cleanup();
    close(app.channel);
    return 0;
}
