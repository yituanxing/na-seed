/* Host-only ABI/keyboard acceptance for the same freestanding VT implementation
 * the native kernels link. Never substitutes for real QMP keyboard and pixels. */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/task.h"
#include "kernel/errno.h"
#include "kernel/drivers/vt.h"
#include "kernel/drivers/tty.h"
#include "kernel/drivers/core/chrdev.h"

struct nas_vt_mode_test {
    uint8_t mode,waitv;
    uint16_t relsig,acqsig,frsig;
};
struct nas_vt_stat_test {uint16_t active,signal,state;};
_Static_assert(sizeof(struct nas_vt_mode_test)==8,"VT mode ABI");
_Static_assert(sizeof(struct nas_vt_stat_test)==6,"VT state ABI");

static struct nas_task process;
struct nas_task *nas_current_task=&process;
static void (*input_handler)(uint16_t,bool);
static unsigned published;
static bool fbcon_active,fbcon_graphics;
static void (*fbcon_reply)(const uint8_t *,uint64_t);
void nas_fbcon_activate(bool enabled) {fbcon_active=enabled;}
void nas_fbcon_set_graphics(bool graphics) {fbcon_graphics=graphics;}
void nas_fbcon_set_report_sink(void (*sink)(const uint8_t *,uint64_t)) {
    fbcon_reply=sink;
}
uint64_t nas_fbcon_write(const uint8_t *src,uint64_t len) {
    assert(src || !len);
    assert(fbcon_active);
    return len;
}
uint64_t nas_arch_monotonic_ns(void) {return 1234000000ULL;}
bool nas_user_write_bytes(uint64_t addr,const uint8_t *src,uint64_t len) {
    memcpy((void *)(uintptr_t)addr,src,(size_t)len);return true;
}
bool nas_user_read_bytes(uint64_t addr,uint8_t *dst,uint64_t len) {
    memcpy(dst,(const void *)(uintptr_t)addr,(size_t)len);return true;
}
int nas_chrdev_node_register(struct nas_chrdev_node *node) {
    assert(node && node->info.fd_kind==23);
    if(published==0){assert(!strcmp(node->path,"/dev/tty0"));assert(node->info.rdev==(4U<<8));}
    if(published==1){assert(!strcmp(node->path,"/dev/tty1"));assert(node->info.rdev==((4U<<8)|1U));}
    published++;return 0;
}
int nas_input_set_keyboard_handler(void (*handler)(uint16_t,bool)) {
    assert(!input_handler);input_handler=handler;return 0;
}
struct nas_task *nas_task_at(unsigned slot) {
    return slot==0?&process:0;
}
void nas_print(const char *s) {(void)s;abort();}
_Noreturn void nas_arch_halt(void) {abort();}
bool nas_tty_controlling(void) {return false;}
bool nas_pty_controlling(void) {return false;}

static void send(uint16_t code) {
    assert(input_handler);input_handler(code,true);input_handler(code,false);
}
static void expect_bytes(const char *expected,unsigned n) {
    char bytes[16]={0};
    assert(n<=sizeof(bytes));
    assert((int64_t)nas_vt_read_plan(n,true)==n);
    assert(nas_vt_read((uint8_t *)bytes,n,true)==n);
    assert(!memcmp(bytes,expected,n));
}
int main(void) {
    process.pid=100;process.sid=100;process.pgid=100;
    nas_vt_init();
    assert(fbcon_reply);
    assert(nas_vt_chrdev_init()==0 && published==2);
    assert(nas_vt_read_waitable());
    uint32_t ctty_sid=0;
    nas_vt_open_session(0x100U); /* O_NOCTTY must not claim the VT */
    assert((int64_t)nas_vt_ioctl(0x5429,(uint64_t)(uintptr_t)&ctty_sid)==-ENOTTY);
    assert(!fbcon_active);
    /* Upstream BusyBox getty opens O_NOCTTY and claims with TIOCSCTTY.
     * The actual framebuffer must become writable on that path too. */
    assert(nas_vt_ioctl(0x540e,0)==0);
    assert(nas_vt_ioctl(0x5429,(uint64_t)(uintptr_t)&ctty_sid)==0 && ctty_sid==100);
    assert(fbcon_active);
    assert(nas_vt_write((const uint8_t *)"login:",6)==6);
    assert(nas_vt_ioctl(0x540e,0)==0); /* same SID must not blank again */

    struct nas_vt_stat_test state={0};
    assert(nas_vt_ioctl(0x5603,(uint64_t)(uintptr_t)&state)==0);
    assert(state.active==1 && (state.state&(1U<<1)));
    assert((int64_t)nas_vt_ioctl(0x5606,2)==-EINVAL);
    assert(nas_vt_ioctl(0x5606,1)==0);
    assert((int64_t)nas_vt_ioctl(0x5607,0)==-EINVAL);

    struct nas_vt_mode_test mode={1,0,10,12,0};
    assert(nas_vt_ioctl(0x5602,(uint64_t)(uintptr_t)&mode)==0);
    assert(nas_vt_ioctl(0x4b3a,1)==0); /* KD_GRAPHICS */
    assert(fbcon_graphics);
    int32_t kd=-1;
    assert(nas_vt_ioctl(0x4b3b,(uint64_t)(uintptr_t)&kd)==0 && kd==1);
    process.pid=101;
    assert((int64_t)nas_vt_ioctl(0x4b3a,0)==-EBUSY);
    process.pid=100;

    /* fbi tty_raw(TCSAFLUSH): original termios ABI and nonblocking select */
    struct nas_termios tty={0};
    assert(nas_vt_ioctl(0x5401,(uint64_t)(uintptr_t)&tty)==0);
    tty.lflag &= ~(0000002U|0000010U); /* -ICANON -ECHO */
    tty.cc[6]=1;tty.cc[5]=0;
    assert(nas_vt_ioctl(0x5404,(uint64_t)(uintptr_t)&tty)==0);
    assert((int64_t)nas_vt_read_plan(1,true)==-EAGAIN);
    /* fbcon's CSI 6n reply must traverse native VT input and N_TTY. */
    fbcon_reply((const uint8_t *)"\033[3;11R",7);
    expect_bytes("\033[3;11R",7);
    send(16);expect_bytes("q",1);
    send(57);expect_bytes(" ",1);
    send(106);expect_bytes("\x1b[C",3);
    input_handler(42,true);send(16);input_handler(42,false);
    expect_bytes("Q",1);
    input_handler(29,true);send(46);input_handler(29,false);
    uint64_t sid=0,pgid=0;
    assert(nas_vt_take_signal(&sid,&pgid)==2);
    assert(nas_vt_take_signal(&sid,&pgid)==0);

    /* Unexpected viewer death releases graphical owner and raw N_TTY. */
    nas_vt_process_exit(100);
    assert(!fbcon_graphics);
    assert(nas_vt_ioctl(0x4b3b,(uint64_t)(uintptr_t)&kd)==0 && kd==0);
    assert(nas_vt_ioctl(0x5401,(uint64_t)(uintptr_t)&tty)==0);
    assert((tty.lflag&0000002U)!=0); /* canonical restored */
    mode=(struct nas_vt_mode_test){0};
    assert(nas_vt_ioctl(0x5601,(uint64_t)(uintptr_t)&mode)==0);
    assert(mode.mode==0);

    send(16);assert((int64_t)nas_vt_read_plan(1,true)==-EAGAIN);
    send(28);expect_bytes("q\n",2);
    assert(nas_vt_write((const uint8_t *)"abc",3)==3);
    nas_vt_session_leader_exit(100);
    assert(!fbcon_active);
    assert((int64_t)nas_vt_ioctl(0x5429,(uint64_t)(uintptr_t)&ctty_sid)==-ENOTTY);
    nas_vt_open_session(0); /* plain session-leader open still works */
    assert(fbcon_active);
    nas_vt_session_leader_exit(100);
    puts("[M202 VT ABI/KEYBOARD HOST PASS] single VT, isolation, raw keys and cleanup");
    return 0;
}
