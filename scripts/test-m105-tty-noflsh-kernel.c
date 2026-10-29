/* Production-model test: link the REAL physical UART TTY and N_TTY sources.
 * Only hardware RX, Task identity, usercopy, and monotonic clock are mocked.
 * M23 keeps the independent genuine three-ISA guest Ctrl-C acceptance. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/task.h"
#include "../kernel/errno.h"
#include "../kernel/drivers/tty.h"
#include "../kernel/drivers/tty_ldisc.h"

static struct nas_task owner={.pid=41,.sid=41,.pgid=41};
struct nas_task *nas_current_task=&owner;
static const uint8_t *rx;
static unsigned rx_remaining;
void nas_putchar(char c){(void)c;}
void nas_arch_console_putc(char c){(void)c;}
bool nas_arch_console_has_rx(void){return true;}
bool nas_arch_console_getc(uint8_t *out){
    if(!rx_remaining)return false;
    *out=*rx++;rx_remaining--;return true;
}
bool nas_user_address_writable(uint64_t addr){return addr!=0;}
bool nas_user_write_bytes(uint64_t addr,const uint8_t *src,uint64_t n){
    if(!addr)return false;
    memcpy((void *)(uintptr_t)addr,src,(size_t)n);return true;
}
bool nas_user_read_bytes(uint64_t addr,uint8_t *dst,uint64_t n){
    if(!addr)return false;
    memcpy(dst,(const void *)(uintptr_t)addr,(size_t)n);return true;
}
struct nas_task *nas_task_at(unsigned slot){(void)slot;return NULL;}
uint64_t nas_arch_monotonic_ns(void){return 0;}
uint64_t nas_time_add_deadline(uint64_t base,uint64_t delta){return base+delta;}
bool nas_time_deadline_expired(uint64_t now,uint64_t deadline){
    return now>=deadline;
}
static struct nas_termios termios_get(void){
    struct nas_termios t={0};
    assert(nas_tty_ioctl(0x5401,(uint64_t)(uintptr_t)&t)==0);
    return t;
}
static void termios_set(const struct nas_termios *t,bool flush){
    assert(nas_tty_ioctl(flush?0x5404:0x5402,
                         (uint64_t)(uintptr_t)t)==0);
}
static void receive(const uint8_t *data,unsigned length){
    assert(!rx_remaining);
    rx=data;rx_remaining=length;
}
static void pump_all(void){
    for(unsigned i=0;i<20 && rx_remaining;i++)nas_tty_pump();
    assert(!rx_remaining);
}
static void signal_int(void){
    uint64_t sid=0,pgid=0;
    assert(nas_tty_take_signal(&sid,&pgid)==2);
    assert(sid==41 && pgid==41);
    assert(nas_tty_take_signal(&sid,&pgid)==0);
}
static void read_exact(const uint8_t *want,unsigned n){
    uint8_t actual[NAS_LDISC_CAPACITY]={0};
    assert(n<=sizeof(actual));
    assert(nas_tty_read((uint64_t)(uintptr_t)actual,n,false)==n);
    assert(memcmp(actual,want,n)==0);
}
static void unread(void){
    uint8_t b=0;
    assert((int64_t)nas_tty_read((uint64_t)(uintptr_t)&b,1,true)==-EAGAIN);
}
int main(void){
    nas_tty_init();
    assert(nas_tty_ioctl(0x540e,0)==0);
    struct nas_termios t=termios_get();
    t.lflag|=0000200U; /* Linux NOFLSH: preserve N_TTY and staged RX. */
    termios_set(&t,false);
    const uint8_t kept[]={'A',3,'B','\r'};
    receive(kept,sizeof(kept));pump_all();signal_int();
    nas_tty_pump(); /* process the staged B + CR after signal dispatch */
    const uint8_t ab[]={'A','B','\n'};
    read_exact(ab,sizeof(ab));

    t.lflag&=~0000200U; /* Default Ctrl-C flushes BOTH queues. */
    termios_set(&t,true);
    const uint8_t flushed[]={'C',3,'D','\r'};
    receive(flushed,sizeof(flushed));pump_all();signal_int();
    nas_tty_pump();unread();
    const uint8_t fresh[]={'E','\r'};
    receive(fresh,sizeof(fresh));pump_all();
    const uint8_t e[]={'E','\n'};
    read_exact(e,sizeof(e));

    /* A completed canonical line cannot hide the following VINTR. */
    t.lflag|=0000200U;
    termios_set(&t,false);
    const uint8_t ready_line[]={'F','\r',3};
    receive(ready_line,sizeof(ready_line));pump_all();signal_int();
    const uint8_t f[]={'F','\n'};
    read_exact(f,sizeof(f));
    t.lflag&=~0000200U;
    termios_set(&t,false);
    const uint8_t ready_flush[]={'G','\r',3};
    receive(ready_flush,sizeof(ready_flush));pump_all();signal_int();
    unread();

    /* Even a completely full raw line discipline recognizes VINTR.
     * NOFLSH must retain every one of the 256 unread data bytes. */
    t.lflag=(t.lflag|0000200U)&~2U; /* NOFLSH, !ICANON, ISIG */
    t.cc[6]=1; /* VMIN */
    termios_set(&t,true);
    uint8_t full[NAS_LDISC_CAPACITY+1];
    memset(full,'J',NAS_LDISC_CAPACITY);
    full[NAS_LDISC_CAPACITY]=3;
    receive(full,sizeof(full));pump_all();signal_int();
    read_exact(full,NAS_LDISC_CAPACITY);
    puts("[M105 PHYSICAL TTY NOFLSH HOST PASS] keep/flush, ready-line ISIG, full-buffer ISIG");
    return 0;
}
