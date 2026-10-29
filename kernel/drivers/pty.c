#include <stdint.h>
#include <stdbool.h>
#include "../errno.h"
#include "../fd.h"
#include "../task.h"
#include "../mm/usercopy.h"
#include "../lib/string.h"
#include "tty.h"
#include "tty_ldisc.h"
#include "pty.h"

/* One multiplexor with N independently allocated master/slave pairs.
 * Two real byte queues transfer data; a slave-side line discipline and
 * termios belong to the device, never to an ISA-specific syscall number.
 * All endpoints reference shared open-description identities. */
#define PTY_RING_CAP 2048U
/* A bounded cross-PTY queue owns event order until the scheduler is safely
 * outside the writer's syscall C frame. Never overwrite an unread signal. */
#define PTY_SIGNAL_FIFO_CAP 64U
struct pty_signal_event {uint64_t sid,pgid;unsigned signo;};
static struct pty_signal_event signal_fifo[PTY_SIGNAL_FIFO_CAP];
static unsigned signal_fifo_head,signal_fifo_count;
struct pty_ring { uint8_t bytes[PTY_RING_CAP]; unsigned head,count; };
struct pty {
    bool used,master_open,locked;
    unsigned slave_descriptions;
    uint64_t sid,pgid;
    struct nas_termios termios;
    uint16_t winsize[4];
    struct nas_ldisc input;
    struct pty_ring master_input,slave_input;
};
static struct pty ptys[NAS_PTY_LIMIT];
static uint8_t slot_device[NAS_OPEN_FILE_LIMIT]; /* index + 1 */
static uint8_t slot_is_master[NAS_OPEN_FILE_LIMIT];

static bool pty_queue_signal(const struct pty *p,unsigned signum) {
    /* No controlling session / foreground process group: consume the
     * ISIG byte but never deliver it to another, unrelated session. */
    if(!p->sid || !p->pgid)return true;
    if(signal_fifo_count==PTY_SIGNAL_FIFO_CAP)return false;
    unsigned tail=(signal_fifo_head+signal_fifo_count)%PTY_SIGNAL_FIFO_CAP;
    signal_fifo[tail]=(struct pty_signal_event){p->sid,p->pgid,signum};
    signal_fifo_count++;
    return true;
}
unsigned nas_pty_take_signal(uint64_t *sid,uint64_t *pgid) {
    if(!sid || !pgid || !signal_fifo_count)return 0;
    struct pty_signal_event e=signal_fifo[signal_fifo_head];
    signal_fifo_head=(signal_fifo_head+1)%PTY_SIGNAL_FIFO_CAP;
    signal_fifo_count--;
    *sid=e.sid;*pgid=e.pgid;
    return e.signo;
}

static struct pty *get(unsigned slot) {
    unsigned id=slot<NAS_OPEN_FILE_LIMIT?slot_device[slot]:0;
    return id && id<=NAS_PTY_LIMIT && ptys[id-1].used ? &ptys[id-1]:0;
}
unsigned nas_pty_number(unsigned slot) {
    return slot<NAS_OPEN_FILE_LIMIT && slot_device[slot] ?
           (unsigned)slot_device[slot]-1 : NAS_PTY_LIMIT;
}
bool nas_pty_present(unsigned number) {
    return number<NAS_PTY_LIMIT && ptys[number].used && ptys[number].master_open;
}
int nas_pty_path_number(const char *path) {
    const char *prefix="/dev/pts/";
    unsigned i=0;
    while(prefix[i] && path[i]==prefix[i])i++;
    if(prefix[i] || !path[i])return -1;
    unsigned value=0;
    for(;path[i];i++) {
        if(path[i]<'0'||path[i]>'9')return -1;
        value=value*10+(unsigned)(path[i]-'0');
        if(value>=NAS_PTY_LIMIT)return -1;
    }
    return (int)value;
}
static void init_termios(struct nas_termios *t) {
    memset(t,0,sizeof(*t));
    t->iflag=0000400; /* ICRNL */
    t->oflag=0000001|0000004; /* OPOST | ONLCR */
    t->cflag=0000060|0000200|0004000|0000017;
    t->lflag=0000001|0000002|0000010; /* ISIG | ICANON | ECHO */
    t->cc[0]=3;t->cc[1]=28;t->cc[2]=127;t->cc[3]=21;
    t->cc[4]=4;t->cc[6]=1;t->cc[10]=26;t->cc[14]=23;
}
int nas_pty_new(unsigned slot) {
    if(slot>=NAS_OPEN_FILE_LIMIT || slot_device[slot])return -EINVAL;
    for(unsigned i=0;i<NAS_PTY_LIMIT;i++) {
        if(ptys[i].used)continue;
        struct pty *p=&ptys[i];
        *p=(struct pty){0};
        p->used=p->master_open=p->locked=true;
        p->winsize[0]=24;p->winsize[1]=80;
        init_termios(&p->termios);
        nas_ldisc_reset(&p->input);
        slot_device[slot]=(uint8_t)(i+1);
        slot_is_master[slot]=1;
        return 0;
    }
    return -ENOSPC;
}
int nas_pty_attach(unsigned number,unsigned slot) {
    if(slot>=NAS_OPEN_FILE_LIMIT || slot_device[slot])return -EINVAL;
    if(!nas_pty_present(number))return -ENOENT;
    struct pty *p=&ptys[number];
    if(p->locked)return -EIO;
    slot_device[slot]=(uint8_t)(number+1);
    slot_is_master[slot]=0;
    p->slave_descriptions++;
    return 0;
}
bool nas_pty_controlling(void) {
    uint64_t sid=nas_current_task->sid;
    if(!sid)return false;
    for(unsigned i=0;i<NAS_PTY_LIMIT;i++)
        if(ptys[i].used && ptys[i].sid==sid)return true;
    return false;
}
int nas_pty_attach_controlling(unsigned slot) {
    uint64_t sid=nas_current_task->sid;
    for(unsigned i=0;sid && i<NAS_PTY_LIMIT;i++)
        if(ptys[i].used && ptys[i].sid==sid)
            return nas_pty_attach(i,slot);
    return -ENXIO;
}
void nas_pty_release(unsigned slot) {
    struct pty *p=get(slot);
    if(!p)return;
    if(slot_is_master[slot])p->master_open=false;
    else if(p->slave_descriptions)p->slave_descriptions--;
    slot_device[slot]=slot_is_master[slot]=0;
    if(!p->master_open && !p->slave_descriptions)*p=(struct pty){0};
}
static void ring_put(struct pty_ring *r,uint8_t value) {
    r->bytes[(r->head+r->count)%PTY_RING_CAP]=value;
    r->count++;
}
static uint8_t ring_take(struct pty_ring *r) {
    uint8_t value=r->bytes[r->head];
    r->head=(r->head+1)%PTY_RING_CAP;
    r->count--;
    return value;
}
/* Terminal output from slave is the master's actual input. */
static void pty_echo(void *context,uint8_t value) {
    struct pty *p=(struct pty *)context;
    if(p->master_input.count<PTY_RING_CAP)ring_put(&p->master_input,value);
}
bool nas_pty_read_ready(unsigned slot) {
    struct pty *p=get(slot);
    if(!p)return true;
    if(slot_is_master[slot])
        return p->master_input.count || !p->slave_descriptions;
    if(p->termios.lflag&0000002)
        return p->input.ready || p->input.eof || !p->master_open;
    return p->slave_input.count || !p->master_open;
}
bool nas_pty_write_ready(unsigned slot) {
    struct pty *p=get(slot);
    if(!p)return true; /* wake a blocked writer to report EBADF */
    if(slot_is_master[slot]) {
        if(!p->slave_descriptions)return true; /* wake to report EIO */
        if(signal_fifo_count==PTY_SIGNAL_FIFO_CAP)return false;
        /* Canonical input is owned by N_TTY, not the raw slave ring.
         * A completed line waits for its reader and must not silently
         * consume further master writes or advertise false POLLOUT. */
        if(p->termios.lflag&0000002)
            return !p->input.ready && p->input.used<NAS_LDISC_CAPACITY;
        return p->slave_input.count<PTY_RING_CAP-2;
    }
    return !p->master_open || p->master_input.count<PTY_RING_CAP-2;
}
uint16_t nas_pty_poll(unsigned slot,uint16_t events) {
    struct pty *p=get(slot);
    if(!p)return 0x0020U; /* POLLNVAL, not a permanently asleep waiter */
    bool peer_closed=slot_is_master[slot] ?
        !p->slave_descriptions : !p->master_open;
    /* Linux poll reports HUP/ERR even when the caller requested only
     * POLLIN/POLLOUT. A disconnected peer is not a writable stream. */
    if(peer_closed)return 0x0018U; /* POLLERR | POLLHUP */
    uint16_t ready=0;
    if((events&1U) && nas_pty_read_ready(slot))ready|=1U;
    if((events&4U) && nas_pty_write_ready(slot))ready|=4U;
    return ready;
}
uint64_t nas_pty_read(uint64_t fd,uint64_t dst,uint64_t count) {
    if(fd>=NAS_FD_LIMIT || !count)return 0;
    unsigned slot=nas_current_task->fds->entries[fd].slot;
    struct pty *p=get(slot);
    if(!p)return (uint64_t)-EBADF;
    bool master=slot_is_master[slot]!=0;
    struct pty_ring *ring=master?&p->master_input:&p->slave_input;
    if(!master && (p->termios.lflag&0000002)) {
        if(p->input.eof && !p->input.used) {
            nas_ldisc_reset(&p->input);return 0;
        }
        if(!nas_ldisc_readable(&p->input,&p->termios,(unsigned)(count>256?256:count)))
            return p->master_open ? (uint64_t)-EAGAIN : 0;
        uint8_t buffer[NAS_LDISC_CAPACITY];
        unsigned n=nas_ldisc_peek(&p->input,&p->termios,buffer,
                                   count>sizeof(buffer)?sizeof(buffer):(unsigned)count);
        if(!nas_user_write_bytes(dst,buffer,n))return (uint64_t)-EFAULT;
        nas_ldisc_commit(&p->input,n);
        return n;
    }
    if(!ring->count)
        return (master?p->slave_descriptions:p->master_open) ?
               (uint64_t)-EAGAIN : master?(uint64_t)-EIO:0;
    unsigned n=count>ring->count?ring->count:(unsigned)count;
    /* Peek the complete bounded ring read before committing any bytes.
     * Per-byte user writes repeatedly resolved the same PTE and could
     * partially modify the destination while replaying the entire ring on
     * EFAULT. usercopy now validates writable/COW pages as one transaction. */
    uint8_t bytes[PTY_RING_CAP];
    unsigned first=n;
    if(first>PTY_RING_CAP-ring->head)first=PTY_RING_CAP-ring->head;
    memcpy(bytes,ring->bytes+ring->head,first);
    if(n>first)memcpy(bytes+first,ring->bytes,n-first);
    if(!nas_user_write_bytes(dst,bytes,n))return (uint64_t)-EFAULT;
    for(unsigned i=0;i<n;i++)(void)ring_take(ring);
    return n;
}
uint64_t nas_pty_write(uint64_t fd,uint64_t src,uint64_t count) {
    if(fd>=NAS_FD_LIMIT || !count)return 0;
    unsigned slot=nas_current_task->fds->entries[fd].slot;
    struct pty *p=get(slot);
    if(!p)return (uint64_t)-EBADF;
    bool master=slot_is_master[slot]!=0;
    if((master && !p->slave_descriptions) || (!master && !p->master_open))
        return (uint64_t)-EIO;
    struct pty_ring *r=master?&p->slave_input:&p->master_input;
    unsigned done=0;
    while(done<count) {
        uint8_t value;
        if(!nas_user_read_bytes(src+done,&value,1))
            return done?done:(uint64_t)-EFAULT;
        if(master) {
            /* N_TTY generates terminal signals with ICANON on OR off.
             * The master is often a shell/tmux in a different process
             * group; never synchronously kill the currently executing
             * writer's Task inside this syscall. */
            enum nas_ldisc_event ev=nas_ldisc_signal_event(&p->termios,value);
            if(ev!=NAS_LDISC_OK) {
                unsigned signo=ev==NAS_LDISC_SIGNAL_INT?2U:
                    ev==NAS_LDISC_SIGNAL_QUIT?3U:20U;
                if(!pty_queue_signal(p,signo))break; /* bounded, no loss */
                /* Linux NOFLSH (lflag bit 0x80) preserves both canonical
                 * and noncanonical unread input when ISIG is generated.
                 * The default clears both queues, including typed data
                 * ahead of the signal byte. */
                if(!(p->termios.lflag&0000200U)) {
                    nas_ldisc_reset(&p->input);
                    p->slave_input.head=p->slave_input.count=0;
                }
                done++;
                continue;
            }
        }
        if(master && !(p->termios.lflag&0000002)) {
            /* Noncanonical PTY input still obeys the same ICRNL/IGNCR/
             * INLCR rules as the canonical line discipline. ISIG already
             * inspected the original byte above, before translation. */
            if(value=='\r') {
                if(p->termios.iflag&0000200) {done++;continue;}
                if(p->termios.iflag&0000400)value='\n';
            } else if(value=='\n' && (p->termios.iflag&0000100))value='\r';
        }
        if(master && (p->termios.lflag&0000002)) {
            /* nas_ldisc_feed() intentionally rejects input after a full
             * canonical line. Do not count a rejected byte as written. */
            if(p->input.ready || p->input.used==NAS_LDISC_CAPACITY)break;
            enum nas_ldisc_event ev=nas_ldisc_feed(&p->input,&p->termios,
                                                    value,pty_echo,p);
            /* Already classified above, including translated CR/NL and
             * noncanonical ISIG. This branch only appends ordinary data. */
            if(ev!=NAS_LDISC_OK)return done?done:(uint64_t)-EIO;
        }else{
            if(r->count>=PTY_RING_CAP-2)break;
            if(!master && value=='\n' && (p->termios.oflag&5U)==5U)
                ring_put(r,'\r');
            ring_put(r,value);
        }
        done++;
    }
    return done?done:(uint64_t)-EAGAIN;
}
static uint64_t copy_u32(uint64_t ptr,uint32_t value) {
    return nas_user_write_bytes(ptr,(const uint8_t *)&value,4)?0:(uint64_t)-EFAULT;
}
uint64_t nas_pty_ioctl(uint64_t fd,uint64_t cmd,uint64_t arg) {
    if(fd>=NAS_FD_LIMIT)return (uint64_t)-EBADF;
    unsigned slot=nas_current_task->fds->entries[fd].slot;
    struct pty *p=get(slot);
    if(!p)return (uint64_t)-EBADF;
    bool master=slot_is_master[slot]!=0;
    switch(cmd) {
    case 0x80045430ULL: /* TIOCGPTN */
        return master?copy_u32(arg,nas_pty_number(slot)):(uint64_t)-ENOTTY;
    case 0x40045431ULL: { /* TIOCSPTLCK */
        if(!master)return (uint64_t)-ENOTTY;
        uint32_t lock;
        if(!nas_user_read_bytes(arg,(uint8_t *)&lock,4))return (uint64_t)-EFAULT;
        p->locked=lock!=0;return 0;
    }
    case 0x80045439ULL: /* TIOCGPTLCK */
        return master?copy_u32(arg,p->locked?1U:0U):(uint64_t)-ENOTTY;
    case 0x5401ULL: /* TCGETS */
        return nas_user_write_bytes(arg,(const uint8_t *)&p->termios,36)
               ? 0:(uint64_t)-EFAULT;
    case 0x5402ULL:case 0x5403ULL:case 0x5404ULL: { /* TCSETS */
        struct nas_termios new_termios;
        if(!nas_user_read_bytes(arg,(uint8_t *)&new_termios,36))
            return (uint64_t)-EFAULT;
        if(new_termios.line)return (uint64_t)-EINVAL;
        p->termios=new_termios;
        if(cmd==0x5404ULL)nas_ldisc_reset(&p->input);
        return 0;
    }
    case 0x5413ULL: /* TIOCGWINSZ */
        return nas_user_write_bytes(arg,(const uint8_t *)p->winsize,8)
               ? 0:(uint64_t)-EFAULT;
    case 0x5414ULL: /* TIOCSWINSZ */
        return nas_user_read_bytes(arg,(uint8_t *)p->winsize,8)
               ? 0:(uint64_t)-EFAULT;
    case 0x540eULL: /* TIOCSCTTY */
        if(master)return (uint64_t)-ENOTTY;
        if(nas_current_task->pid!=nas_current_task->sid)return (uint64_t)-EPERM;
        if(p->sid && p->sid!=nas_current_task->sid)return (uint64_t)-EPERM;
        p->sid=nas_current_task->sid;
        p->pgid=nas_current_task->pgid;
        return 0;
    case 0x5422ULL: /* TIOCNOTTY */
        if(p->sid!=nas_current_task->sid)return (uint64_t)-ENOTTY;
        if(nas_current_task->pid==p->sid)p->sid=p->pgid=0;
        return 0;
    case 0x5429ULL: /* TIOCGSID */
        return p->sid==nas_current_task->sid?
               copy_u32(arg,(uint32_t)p->sid):(uint64_t)-ENOTTY;
    case 0x540fULL: /* TIOCGPGRP */
        return p->sid==nas_current_task->sid?
               copy_u32(arg,(uint32_t)p->pgid):(uint64_t)-ENOTTY;
    case 0x5410ULL: { /* TIOCSPGRP */
        if(p->sid!=nas_current_task->sid)return (uint64_t)-ENOTTY;
        uint32_t group;
        if(!nas_user_read_bytes(arg,(uint8_t *)&group,4))return (uint64_t)-EFAULT;
        if(!group)return (uint64_t)-EINVAL;
        for(unsigned i=0;i<NAS_TASK_LIMIT;i++) {
            struct nas_task *task=nas_task_at(i);
            if(task && task->state!=NAS_TASK_FREE && task->sid==p->sid &&
               task->pgid==group){p->pgid=group;return 0;}
        }
        return (uint64_t)-EPERM;
    }
    default:return (uint64_t)-ENOTTY;
    }
}
