#include <stdint.h>
#include <stdbool.h>
#include "../task.h"
#include "../errno.h"
#include "../arch/console.h"
#include "../arch/mmu.h"
#include "../mm/usercopy.h"
#include "serial.h"
#include "tty.h"
/* One console TTY: POSIX session/foreground ownership and native Linux
 * TCGETS wire ABI are generic. Only x86 COM1 offers real physical RX now;
 * RV SBI and ARM semihost transports explicitly do not pretend input. */
static struct {
    uint64_t sid,pgid;
    struct nas_termios t;
    bool rx_enabled,done,eof;
    uint8_t line[256];
    unsigned size,pos;
} tty;
void nas_tty_init(void) {
    tty.sid=nas_current_task->sid; tty.pgid=nas_current_task->pgid;
    tty.rx_enabled=tty.done=tty.eof=false; tty.size=tty.pos=0;
    /* Freestanding RV64/ARM64 kernel links no libc memcpy/memset.
     * Explicit field/CC initialization avoids compiler-emitted libc calls. */
    tty.t.iflag=tty.t.oflag=tty.t.cflag=tty.t.lflag=0;
    tty.t.line=0;
    for(unsigned i=0;i<19;i++)tty.t.cc[i]=0;
    tty.t.iflag=0000400; /* ICRNL */
    tty.t.oflag=0000001|0000004; /* OPOST|ONLCR */
    tty.t.cflag=0000060|0000200|0004000|0000017; /* CS8|CREAD|CLOCAL|B38400 */
    tty.t.lflag=0000001|0000002|0000010; /* ISIG|ICANON|ECHO */
    tty.t.cc[0]=3; tty.t.cc[2]=127; tty.t.cc[4]=4; tty.t.cc[6]=1;
}
bool nas_tty_controlling(void) {
    return tty.sid && tty.sid==nas_current_task->sid;
}
bool nas_tty_onlcr(void) {
    return tty.rx_enabled && (tty.t.oflag&5U)==5U;
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
        if(cmd==0x5404) tty.size=tty.pos=0,tty.done=tty.eof=false;
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
uint64_t nas_tty_read(uint64_t dst,uint64_t len,bool nonblock) {
    if(!len)return 0;
    if(!tty.rx_enabled)return (uint64_t)-EAGAIN; /* old output-only console */
    if(!nas_arch_console_has_rx())return (uint64_t)-EOPNOTSUPP;
    if(dst>UINT64_MAX-len)return (uint64_t)-EFAULT;
    for(uint64_t i=0;i<len;i+=4096)
        if(!nas_mmu_user_writable(dst+i))return (uint64_t)-EFAULT;
    if(!nas_mmu_user_writable(dst+len-1))return (uint64_t)-EFAULT;
    while(!tty.done) {
        uint8_t ch;
        if(!nas_arch_console_getc(&ch)) {
            if(nonblock)return (uint64_t)-EAGAIN;
            continue; /* no fake bytes, real COM1 RX */
        }
        if((tty.t.iflag&0000400) && ch=='\r')ch='\n';
        if((tty.t.lflag&0000001) && ch==tty.t.cc[0])
            return (uint64_t)-EOPNOTSUPP; /* SIGINT frame not yet implemented */
        if((tty.t.lflag&0000002) && ch==tty.t.cc[2]) {
            if(tty.size) { tty.size--;
                if(tty.t.lflag&0000010) {
                    nas_putchar('\b');nas_putchar(' ');nas_putchar('\b');
                }
            }
            continue;
        }
        if((tty.t.lflag&0000002) && ch==tty.t.cc[4]) {
            tty.eof=tty.size==0;tty.done=true;break;
        }
        if(tty.size<sizeof tty.line) {
            tty.line[tty.size++]=ch;
            if(tty.t.lflag&0000010) {
                if(ch=='\n')nas_putchar('\r');
                nas_putchar((char)ch);
            }
        }
        if(!(tty.t.lflag&0000002) || ch=='\n' ||
           tty.size==sizeof tty.line)tty.done=true;
    }
    if(tty.eof) {
        tty.eof=tty.done=false;tty.size=tty.pos=0;return 0;
    }
    uint64_t count=tty.size-tty.pos;
    if(count>len)count=len;
    if(!nas_user_write_bytes(dst,tty.line+tty.pos,count))
        return (uint64_t)-EFAULT;
    tty.pos+=(unsigned)count;
    if(tty.pos==tty.size)tty.size=tty.pos=0,tty.done=false;
    return count;
}
