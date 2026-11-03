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
#include <unistd.h>
#include "lvgl.h"

/* This is an ACTUAL upstream LVGL 9.4 client, not hand-painted demo glyphs.
 * The desktop and kernel never see LVGL objects. The client alone owns the
 * LVGL input device, display and XRGB8888 anonymous shared memory. */
enum { GUI_W=240, GUI_H=176, GUI_DRAW_ROWS=44 };
struct lvgl_app {
    int channel,surface_fd,flushed,clicks;
    uint8_t *surface;
    uint8_t draw[GUI_W*GUI_DRAW_ROWS*4U];
    int pointer_x,pointer_y,pointer_down;
    lv_display_t *display;
    lv_indev_t *indev;
    lv_obj_t *button,*status;
};
static struct lvgl_app app={.channel=-1,.surface_fd=-1};

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
static void pump_lvgl(void) {
    /* This widget session is input-triggered; there is no animation or
     * periodic redraw. Feed a monotonic 40 ms tick for every input phase. */
    for(unsigned i=0;i<3;i++) {
        lv_tick_inc(40);
        lv_timer_handler();
    }
}
static void init_lvgl_surface(void) {
    app.surface=nasd_make_surface("m171-real-lvgl",GUI_W,GUI_H,
                                   &app.surface_fd);
    nasd_fill_surface(app.surface,GUI_W,GUI_H,20,32,50);
    lv_init();
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

    lv_obj_t *screen=lv_screen_active();
    lv_obj_set_style_bg_color(screen,lv_color_hex(0x14243a),0);
    lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
    lv_obj_t *heading=lv_label_create(screen);
    lv_label_set_text(heading,"REAL LVGL 9.4");
    lv_obj_set_style_text_color(heading,lv_color_hex(0xf0f8ff),0);
    lv_obj_align(heading,LV_ALIGN_TOP_MID,0,9);
    app.button=lv_button_create(screen);
    lv_obj_set_size(app.button,198,66);
    lv_obj_align(app.button,LV_ALIGN_CENTER,0,11);
    lv_obj_set_style_bg_color(app.button,lv_color_hex(0x1668a0),0);
    lv_obj_add_event_cb(app.button,button_clicked,LV_EVENT_CLICKED,NULL);
    lv_obj_t *button_text=lv_label_create(app.button);
    lv_label_set_text(button_text,"CLICK HERE");
    lv_obj_center(button_text);
    app.status=lv_label_create(screen);
    lv_label_set_text(app.status,"CLICKS: 0");
    lv_obj_set_style_text_color(app.status,lv_color_hex(0xf5e4b4),0);
    lv_obj_align(app.status,LV_ALIGN_BOTTOM_MID,0,-5);
    pump_lvgl();
    nasd_require(app.flushed && app.surface[3]==255,
                 "real LVGL initial pixels reached shared surface");
    app.flushed=0;
}
static void set_pointer(unsigned x,unsigned y,int down) {
    nasd_require(x<GUI_W && y<GUI_H &&
                 (down==0 || down==1),"LVGL client pointer coordinates");
    app.pointer_x=(int)x;app.pointer_y=(int)y;
    app.pointer_down=down;
    pump_lvgl();
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
    nasd_require(app.clicks==1 && app.flushed &&
                 surface_sum()!=before,
                 "original LVGL pointer event repainted real XRGB pixels");
    cleanup();
    nasd_mark("[M171 HOST LVGL PASS] pinned upstream LVGL linked, "
              "pointer button click and real XRGB memfd flush");
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
                           NASD_INPUT_POINTER);
    nasd_mark("M171_LVGL_REAL_RENDER_PASS");
    for(;;) {
        struct message m=nasd_recv_packet(app.channel);
        if(m.type==MSG_QUIT)break;
        nasd_require(m.type==MSG_POINTER && m.key<=1,
                     "LVGL client pointer IPC");
        const unsigned x=m.value>>16U,y=m.value&65535U;
        int before=app.clicks;
        set_pointer(x,y,(int)m.key);
        if(app.flushed) {
            __atomic_thread_fence(__ATOMIC_RELEASE);
            nasd_send_packet(app.channel,MSG_UPDATED,
                             app.clicks>before?KEY_ENTER:0);
            app.flushed=0;
        }
        if(app.clicks>before && app.clicks==1)
            nasd_mark("M171_LVGL_REAL_BUTTON_CLICK_PASS");
    }
    cleanup();
    close(app.channel);
    return 0;
}
