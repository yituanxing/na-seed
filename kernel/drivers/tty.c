#include <stdint.h>
#include <stdbool.h>
#include "../task.h"
#include "../errno.h"
#include "../arch/console.h"
#include "../arch/mmu.h"
#include "../mm/usercopy.h"
#include "serial.h"
#include "tty.h"
#include "tty_ldisc.h"
/* One console TTY: POSIX session/foreground ownership and native Linux
 * TCGETS wire ABI are common. x86 COM1, RV SBI firmware console_getchar
 * and ARM PL011 provide actual RX bytes via the same nonblocking interface. */
static struct {
    uint64_t sid,pgid;
    struct nas_termios t;
    bool rx_enabled;
    bool unsupported_input;
    struct nas_ldisc ldisc;
} tty;
void nas_tty_init(void) {
    tty.sid=nas_current_task->sid; tty.pgid=nas_current_task->pgid;
    tty.rx_enabled=false;
    tty.unsupported_input=false;
    nas_ldisc_reset(&tty.ldisc);
    /* Freestanding RV64/ARM64 kernel links no libc memcpy/memset.
     * Explicit field/CC initialization avoids compiler-emitted libc calls. */
    tty.t.iflag=tty.t.oflag=tty.t.cflag=tty.t.lflag=0;
    tty.t.line=0;
    for(unsigned i=0;i<19;i++)tty.t.cc[i]=0;
    tty.t.iflag=0000400; /* ICRNL */
    tty.t.oflag=0000001|0000004; /* OPOST|ONLCR */
    tty.t.cflag=0000060|0000200|0004000|0000017; /* CS8|CREAD|CLOCAL|B38400 */
    tty.t.lflag=0000001|0000002|0000010; /* ISIG|ICANON|ECHO */
    tty.t.cc[0]=3; tty.t.cc[1]=28; tty.t.cc[2]=127; tty.t.cc[3]=21;
    tty.t.cc[4]=4; tty.t.cc[6]=1; tty.t.cc[10]=26; tty.t.cc[14]=23;
}
bool nas_tty_enabled(void) { return tty.rx_enabled; }
bool nas_tty_controlling(void) {
    return tty.sid && tty.sid==nas_current_task->sid;
}
bool nas_tty_onlcr(void) {
    return tty.rx_enabled && (tty.t.oflag&5U)==5U;
}
void nas_tty_write_char(uint8_t byte) {
    if(byte=='\n'&&nas_tty_onlcr())nas_putchar('\r');
    nas_putchar((char)byte);
}
static void tty_echo(void *ignored,uint8_t b) {
    (void)ignored;nas_putchar((char)b);
}
static uint64_t pid_out(uint64_t dst,uint64_t pid) {
    uint32_t x=(uint32_t)pid;
    uint8_t b[4]={(uint8_t)x,(uint8_t)(x>>8),
                  (uint8_t)(x>>16),(uint8_t)(x>>24)};
    return nas_user_write_bytes(dst,b,4)?0:(uint64_t)-EFAULT;
}
static uint64_t pid_in(uint64_t src,int32_t *pid) {
    uint8_t b[4];
    if(!nas_user_read_bytes(src,b,4))return (uint64_t)-EFAULT;
    *pid=(int32_t)((uint32_t)b[0]|(uint32_t)b[1]<<8|
                   (uint32_t)b[2]<<16|(uint32_t)b[3]<<24);
    return 0;
}
uint64_t nas_tty_ioctl(uint64_t cmd,uint64_t arg) {
    struct nas_task *self=nas_current_task;
    switch(cmd) {
    case 0x5401: /* TCGETS */
        return nas_user_write_bytes(arg,(const uint8_t *)&tty.t,36)
               ? 0:(uint64_t)-EFAULT;
    case 0x5402: case 0x5403: case 0x5404: { /* TCSETS/W/F */
        struct nas_termios t;
        if(!nas_user_read_bytes(arg,(uint8_t *)&t,36))
            return (uint64_t)-EFAULT;
        if(t.line) return (uint64_t)-EINVAL;
        tty.t.iflag=t.iflag;tty.t.oflag=t.oflag;
        tty.t.cflag=t.cflag;tty.t.lflag=t.lflag;
        tty.t.line=t.line;
        for(unsigned i=0;i<19;i++)tty.t.cc[i]=t.cc[i];
        if(cmd==0x5404) {
            nas_ldisc_reset(&tty.ldisc);
            tty.unsupported_input=false;
        }
        return 0;
    }
    case 0x540e: /* TIOCSCTTY: only owning session leader */
        if(self->pid!=self->sid) return (uint64_t)-EPERM;
        if(tty.sid && tty.sid!=self->sid) return (uint64_t)-EPERM;
        tty.sid=self->sid;tty.pgid=self->pgid;
        tty.rx_enabled=true; /* explicit switch; old noninteractive tests safe */
        return 0;
    case 0x5422: /* TIOCNOTTY */
        if(!nas_tty_controlling()) return (uint64_t)-ENOTTY;
        if(self->pid==self->sid) {
            tty.sid=tty.pgid=0;tty.rx_enabled=false;
            nas_ldisc_reset(&tty.ldisc);
            tty.unsupported_input=false;
        }
        return 0;
    case 0x5429: /* TIOCGSID */
        return nas_tty_controlling()?pid_out(arg,tty.sid):(uint64_t)-ENOTTY;
    case 0x540f: /* TIOCGPGRP */
        return nas_tty_controlling()?pid_out(arg,tty.pgid):(uint64_t)-ENOTTY;
    case 0x5410: { /* TIOCSPGRP */
        if(!nas_tty_controlling())return (uint64_t)-ENOTTY;
        int32_t group;uint64_t rc=pid_in(arg,&group);
        if((int64_t)rc<0)return rc;
        if(group<=0)return (uint64_t)-EINVAL;
        for(unsigned i=0;i<NAS_TASK_LIMIT;i++){
            struct nas_task *t=nas_task_at(i);
            if(t && t->state!=NAS_TASK_FREE && t->sid==tty.sid &&
               t->pgid==(uint32_t)group) {
                tty.pgid=(uint32_t)group;return 0;
            }
        }
        return (uint64_t)-EPERM;
    }
    default:return (uint64_t)-ENOTTY;
    }
}
/* The bounded transport pump is the ONLY consumer of real console RX.
 * It runs at syscall boundaries and on scheduler/idle wake scans.
 * It never enters a task, uses a user address, or switches CPU context. */
void nas_tty_pump(void) {
    if(!tty.rx_enabled || !nas_arch_console_has_rx() ||
       tty.ldisc.ready || tty.unsupported_input) return;
    for(unsigned i=0;i<32;i++) {
        uint8_t byte;
        if(!nas_arch_console_getc(&byte))break;
        if(nas_ldisc_feed(&tty.ldisc,&tty.t,byte,tty_echo,0)
           ==NAS_LDISC_UNSUPPORTED_SIGNAL) {
            tty.unsupported_input=true;
            break;
        }
        if(tty.ldisc.ready)break;
    }
}
bool nas_tty_read_waitable(void) {
    return tty.rx_enabled && nas_arch_console_has_rx() &&
           ((tty.t.lflag&0000002) || tty.t.cc[5]==0);
}
bool nas_tty_read_ready(uint64_t length) {
    if(!length) return true;
    if(!nas_tty_read_waitable())return true; /* replay a real error */
    nas_tty_pump();
    if(tty.unsupported_input)return true;
    unsigned wanted=length>NAS_LDISC_CAPACITY?
        NAS_LDISC_CAPACITY:(unsigned)length;
    return nas_ldisc_readable(&tty.ldisc,&tty.t,wanted);
}
uint64_t nas_tty_read(uint64_t dst,uint64_t len,bool nonblock) {
    if(!len)return 0;
    if(!tty.rx_enabled)return (uint64_t)-EAGAIN;
    if(!nas_arch_console_has_rx())return (uint64_t)-EOPNOTSUPP;
    /* Timed noncanonical reads require a real clock and timeout source.
     * Do not park an unsupported read or fabricate timeout success. */
    if(!(tty.t.lflag&0000002)&&tty.t.cc[5])
        return (uint64_t)-EOPNOTSUPP;
    if(dst>UINT64_MAX-len)return (uint64_t)-EFAULT;
    for(uint64_t i=0;i<len;i+=4096)
        if(!nas_mmu_user_writable(dst+i))return (uint64_t)-EFAULT;
    if(!nas_mmu_user_writable(dst+len-1))return (uint64_t)-EFAULT;
    nas_tty_pump();
    if(tty.unsupported_input) {
        tty.unsupported_input=false;
        return (uint64_t)-EOPNOTSUPP; /* explicit, never fake ISIG delivery */
    }
    uint8_t bytes[NAS_LDISC_CAPACITY];
    unsigned wanted=len>NAS_LDISC_CAPACITY?NAS_LDISC_CAPACITY:(unsigned)len;
    if(!nas_ldisc_readable(&tty.ldisc,&tty.t,wanted)) {
        /* Noncanonical VMIN=0,VTIME=0 is an immediate 0-byte read.
         * Nonblocking read follows Linux's EAGAIN behavior when empty. */
        if(!(tty.t.lflag&0000002)&&tty.t.cc[6]==0)
            return nonblock?(uint64_t)-EAGAIN:0;
        return (uint64_t)-EAGAIN; /* dispatcher parks only blocking tty */
    }
    if(tty.ldisc.eof) { nas_ldisc_reset(&tty.ldisc);return 0; }
    unsigned n=nas_ldisc_peek(&tty.ldisc,&tty.t,bytes,wanted);
    if(!nas_user_write_bytes(dst,bytes,n))return (uint64_t)-EFAULT;
    nas_ldisc_commit(&tty.ldisc,n);
    return n;
}
