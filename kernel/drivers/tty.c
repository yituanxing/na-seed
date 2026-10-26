#include <stdint.h>
#include <stdbool.h>
#include "../task.h"
#include "../fd.h"
#include "../errno.h"
#include "../arch/console.h"
#include "../arch/mmu.h"
#include "../mm/usercopy.h"
#include "../lib/string.h"
#include "serial.h"
#include "tty.h"
#include "tty_ldisc.h"
#include "../time/clock.h"
#include "../arch/arch.h"
/* One console TTY: POSIX session/foreground ownership and native Linux
 * TCGETS wire ABI are common. x86 COM1, RV SBI firmware console_getchar
 * and ARM PL011 provide actual RX bytes via the same nonblocking interface. */
static struct {
    uint64_t sid,pgid;
    struct nas_termios t;
    uint16_t ws_row,ws_col,ws_xpixel,ws_ypixel;
    bool rx_enabled;
    bool unsupported_input;
    unsigned pending_signum;
    struct nas_ldisc ldisc;
} tty;
void nas_tty_init(void) {
    tty.sid=nas_current_task->sid; tty.pgid=nas_current_task->pgid;
    tty.ws_row=24; tty.ws_col=80; tty.ws_xpixel=tty.ws_ypixel=0;
    tty.rx_enabled=false;
    tty.unsupported_input=false;
    tty.pending_signum=0;
    nas_ldisc_reset(&tty.ldisc);
    /* Initialize native Linux termios with our shared freestanding klib. */
    memset(&tty.t, 0, sizeof(tty.t));
    tty.t.iflag=0000400; /* ICRNL */
    tty.t.oflag=0000001|0000004; /* OPOST|ONLCR */
    tty.t.cflag=0000060|0000200|0004000|0000017; /* CS8|CREAD|CLOCAL|B38400 */
    tty.t.lflag=0000001|0000002|0000010; /* ISIG|ICANON|ECHO */
    tty.t.cc[0]=3; tty.t.cc[1]=28; tty.t.cc[2]=127; tty.t.cc[3]=21;
    tty.t.cc[4]=4; tty.t.cc[6]=1; tty.t.cc[10]=26; tty.t.cc[14]=23;
}
bool nas_tty_enabled(void) { return tty.rx_enabled; }
bool nas_tty_background_fd(uint64_t fd) {
    return tty.rx_enabled && tty.sid &&
        nas_current_task->sid==tty.sid &&
        nas_current_task->pgid!=tty.pgid &&
        fd<NAS_FD_LIMIT &&
        nas_current_task->fds->entries[fd].kind==FD_TTY;
}
bool nas_tty_tostop(void) { return (tty.t.lflag&0000400U)!=0; }
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
            tty.pending_signum=0;
        }
        return 0;
    }
    case 0x5413: { /* TIOCGWINSZ: serial console conventional 80x24 default */
        uint16_t ws[4]={tty.ws_row,tty.ws_col,tty.ws_xpixel,tty.ws_ypixel};
        return nas_user_write_bytes(arg,(const uint8_t *)ws,sizeof(ws))
            ? 0:(uint64_t)-EFAULT;
    }
    case 0x5414: { /* TIOCSWINSZ: set per-console terminal geometry */
        uint16_t ws[4];
        if(!nas_user_read_bytes(arg,(uint8_t *)ws,sizeof(ws)))
            return (uint64_t)-EFAULT;
        if(!ws[0]||!ws[1])return (uint64_t)-EINVAL;
        tty.ws_row=ws[0];tty.ws_col=ws[1];
        tty.ws_xpixel=ws[2];tty.ws_ypixel=ws[3];
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
            tty.pending_signum=0;
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
unsigned nas_tty_take_signal(uint64_t *sid,uint64_t *pgid) {
    unsigned sig=tty.pending_signum;
    if(sig) { *sid=tty.sid; *pgid=tty.pgid; tty.pending_signum=0; }
    return sig;
}
void nas_tty_signal_unsupported(void) { tty.unsupported_input=true; }
/* The bounded transport pump is the ONLY consumer of real console RX.
 * It runs at syscall boundaries and on scheduler/idle wake scans.
 * It never enters a task, uses a user address, or switches CPU context. */
void nas_tty_pump(void) {
    if(!tty.rx_enabled || !nas_arch_console_has_rx() ||
       tty.ldisc.ready || tty.unsupported_input || tty.pending_signum) return;
    for(unsigned i=0;i<32;i++) {
        uint8_t byte;
        if(!nas_arch_console_getc(&byte))break;
        enum nas_ldisc_event e=nas_ldisc_feed(&tty.ldisc,&tty.t,byte,tty_echo,0);
        if(e!=NAS_LDISC_OK) {
            /* Terminal event: do not send control byte to userspace.
             * NOFLSH=0: discard incomplete canonical input on signal. */
            nas_ldisc_reset(&tty.ldisc);
            tty.pending_signum=e==NAS_LDISC_SIGNAL_INT?2U:
                e==NAS_LDISC_SIGNAL_QUIT?3U:20U;
            break;
        }
        if(tty.ldisc.ready)break;
    }
}
void nas_tty_blocking_read_start(struct nas_task *task) {
    task->wait.tty_observed_bytes=0;
    if(!(tty.t.lflag&0000002) && tty.t.cc[5]) {
        unsigned available=tty.ldisc.used-tty.ldisc.pos;
        if(!tty.t.cc[6] || available) {
            task->wait.tty_observed_bytes=available;
            task->wait.deadline_active=true;
            task->wait.deadline_ns=nas_time_add_deadline(nas_arch_monotonic_ns(),
                         (uint64_t)tty.t.cc[5]*100000000ULL);
        }
    }
}
bool nas_tty_read_waitable(void) {
    /* Any supported TTY read may wait: the Task owns the monotonic timer,
     * never the UART device or an unrelated user address space. */
    return tty.rx_enabled && nas_arch_console_has_rx();
}
bool nas_tty_read_ready(struct nas_task *task,uint64_t length) {
    if(!length || !nas_tty_read_waitable())return true;
    nas_tty_pump();
    if(tty.unsupported_input || tty.pending_signum)return true;
    unsigned available=tty.ldisc.used-tty.ldisc.pos;
    if(tty.t.lflag&0000002)return tty.ldisc.ready;
    unsigned need=tty.t.cc[6];
    if(need>length)need=(unsigned)length;
    if(need>NAS_LDISC_CAPACITY)need=NAS_LDISC_CAPACITY;
    if(need==0)need=1;
    if(available>=need)return true;
    if(!tty.t.cc[5])return false;
    /* VMIN=0: total deadline from initial read. VMIN>0: interbyte
     * deadline starts at the first received byte, and resets on progress.
     * 1 decisecond = 100,000,000 actual monotonic nanoseconds. */
    if(tty.t.cc[6] && !available)return false;
    if(!task->wait.deadline_active || available>task->wait.tty_observed_bytes) {
        task->wait.deadline_active=true;
        task->wait.deadline_ns=nas_time_add_deadline(nas_arch_monotonic_ns(),
                    (uint64_t)tty.t.cc[5]*100000000ULL);
        task->wait.tty_observed_bytes=available;
    }
    return nas_time_deadline_expired(nas_arch_monotonic_ns(),task->wait.deadline_ns);
}
bool nas_tty_poll_readable(void) {
    if(!tty.rx_enabled || !nas_arch_console_has_rx())return false;
    nas_tty_pump();
    if(tty.unsupported_input || tty.pending_signum)return true;
    if(tty.t.lflag&0000002)return tty.ldisc.ready;
    /* MIN=0/TIME=0 read returns 0, but poll(POLLIN) must not report
     * a nonexistent byte. Minimum of one physical character for poll. */
    if(tty.ldisc.used==tty.ldisc.pos)return false;
    unsigned min=tty.t.cc[6] ? tty.t.cc[6] : 1;
    return tty.ldisc.used-tty.ldisc.pos>=min;
}
uint64_t nas_tty_read(uint64_t dst,uint64_t len,bool nonblock) {
    if(!len)return 0;
    if(!tty.rx_enabled)return (uint64_t)-EAGAIN;
    if(!nas_arch_console_has_rx())return (uint64_t)-EOPNOTSUPP;
    if(dst>UINT64_MAX-len)return (uint64_t)-EFAULT;
    /* M102: fork COW clears a hardware write bit while the VMA is still
     * logically writable. Terminal reads must not reject that buffer.
     * nas_user_write_bytes() will materialize/break COW atomically BEFORE
     * consuming the canonical input, preserving replay and terminal bytes. */
    for(uint64_t i=0;i<len;i+=4096)
        if(!nas_user_address_writable(dst+i))return (uint64_t)-EFAULT;
    if(!nas_user_address_writable(dst+len-1))return (uint64_t)-EFAULT;
    nas_tty_pump();
    if(tty.unsupported_input) {
        tty.unsupported_input=false;
        return (uint64_t)-EOPNOTSUPP; /* explicit, never fake ISIG delivery */
    }
    uint8_t bytes[NAS_LDISC_CAPACITY];
    unsigned wanted=len>NAS_LDISC_CAPACITY?NAS_LDISC_CAPACITY:(unsigned)len;
    bool noncanon=!(tty.t.lflag&0000002);
    unsigned available=tty.ldisc.used-tty.ldisc.pos;
    bool expired=nas_current_task->wait.deadline_active &&
        nas_time_deadline_expired(nas_arch_monotonic_ns(),
                                  nas_current_task->wait.deadline_ns);
    struct nas_termios copy=tty.t;
    if(noncanon && tty.t.cc[5]) {
        /* VMIN=0/VTIME>0 must NOT complete before the actual timeout.
         * VMIN>0 may return partial input only after interbyte expiry. */
        if(tty.t.cc[6]==0 || (expired && available))copy.cc[6]=1;
        if(expired && !available)return 0;
    }
    if(!nas_ldisc_readable(&tty.ldisc,&copy,wanted)) {
        if(noncanon && !tty.t.cc[6] && !tty.t.cc[5])
            return nonblock?(uint64_t)-EAGAIN:0;
        if(noncanon && tty.t.cc[5] && expired)return 0;
        return (uint64_t)-EAGAIN;
    }
    if(tty.ldisc.eof) { nas_ldisc_reset(&tty.ldisc);return 0; }
    unsigned n=nas_ldisc_peek(&tty.ldisc,&copy,bytes,wanted);
    if(!nas_user_write_bytes(dst,bytes,n))return (uint64_t)-EFAULT;
    nas_ldisc_commit(&tty.ldisc,n);
    return n;
}
