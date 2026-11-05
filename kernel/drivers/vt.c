/* SPDX-License-Identifier: GPL-2.0-only */
/* One real Linux virtual console/seat. Serial getty and its UART N_TTY live
 * in tty.c; this class owns distinct keyboard bytes, termios, VT and KD state.
 * No copied input driver, fake UART keystrokes or successful unsupported
 * multi-console transitions. */
#include "vt.h"
#include "tty.h"
#include "pty.h"
#include "tty_ldisc.h"
#include "core/chrdev.h"
#include "../task.h"
#include "../errno.h"
#include "../mm/usercopy.h"
#include "../lib/string.h"
#include "../time/clock.h"
#include "../arch/arch.h"
#include "../panic.h"
#include "display/fbcon.h"
#ifdef NAS_ENABLE_VIRTIO_INPUT
#include "input/core.h"
#endif

#define NAS_VT_QUEUE_CAP 512U
#define NAS_VT_AUTO 0U
#define NAS_VT_PROCESS 1U
#define NAS_KD_TEXT 0U
#define NAS_KD_GRAPHICS 1U
#define NAS_VT_ACKACQ 2U

/* Linux include/uapi/linux/vt.h; 8 byte structure on every target ISA. */
struct nas_linux_vt_mode {
    uint8_t mode,waitv;
    uint16_t relsig,acqsig,frsig;
};
struct nas_linux_vt_stat { uint16_t active,signal,state; };
_Static_assert(sizeof(struct nas_linux_vt_mode)==8,"Linux vt_mode ABI");
_Static_assert(sizeof(struct nas_linux_vt_stat)==6,"Linux vt_stat ABI");

static struct {
    uint64_t sid,pgid,mode_owner_pid,raw_owner_pid;
    struct nas_termios termios;
    struct nas_linux_vt_mode mode;
    uint16_t rows,cols,xpixel,ypixel;
    unsigned kd_mode,keyboard_mode;
    unsigned pending_signum;
    bool rx_overflow,shift,ctrl,alt,caps,caps_down;
    uint8_t shift_bits,ctrl_bits,alt_bits;
    struct nas_ldisc ldisc;
    uint8_t rx[NAS_VT_QUEUE_CAP];
    unsigned head,count;
} vt;
static void feed_input(void);

static struct nas_chrdev_node vt_nodes[]={
    { .path="/dev/tty0", .info={.fd_kind=23,.unit=0,.dev=2,
       .ino=140,.rdev=(4U<<8),.mode=0020600U} },
    { .path="/dev/tty1", .info={.fd_kind=23,.unit=1,.dev=2,
       .ino=141,.rdev=(4U<<8)|1U,.mode=0020600U} }
};
int nas_vt_chrdev_init(void) {
    for(unsigned i=0;i<sizeof(vt_nodes)/sizeof(vt_nodes[0]);i++) {
        int rc=nas_chrdev_node_register(&vt_nodes[i]);
        if(rc)return rc;
    }
    return 0;
}

static void reset_input(void) {
    nas_ldisc_reset(&vt.ldisc);
    vt.head=vt.count=0;
    vt.rx_overflow=false;
    vt.pending_signum=0;
    vt.shift=vt.ctrl=vt.alt=false;
    vt.caps_down=false;
    vt.shift_bits=vt.ctrl_bits=vt.alt_bits=0;
}
static void reset_graphics_ownership(void) {
    vt.mode=(struct nas_linux_vt_mode){0};
    vt.mode_owner_pid=0;
    vt.kd_mode=NAS_KD_TEXT;
    vt.keyboard_mode=1; /* Linux K_XLATE, never pretend raw scancode output. */
}
static void default_termios(void) {
    memset(&vt.termios,0,sizeof(vt.termios));
    vt.raw_owner_pid=0;
    vt.termios.iflag=0000400U; /* ICRNL */
    vt.termios.oflag=0000001U|0000004U;
    vt.termios.cflag=0000060U|0000200U|0004000U|0000017U;
    vt.termios.lflag=0000001U|0000002U|0000010U;
    vt.termios.cc[0]=3;vt.termios.cc[1]=28;
    vt.termios.cc[2]=127;vt.termios.cc[3]=21;
    vt.termios.cc[4]=4;vt.termios.cc[6]=1;
    vt.termios.cc[10]=26;vt.termios.cc[14]=23;
}
void nas_vt_init(void) {
    memset(&vt,0,sizeof(vt));
    vt.rows=25;vt.cols=80;
    nas_fbcon_activate(false);
    reset_graphics_ownership();
    default_termios();
#ifdef NAS_ENABLE_VIRTIO_INPUT
    /* The input core is hardware-independent and fans out *the same* events
     * to evdev; only the active seat also receives translated terminal keys. */
    if(nas_input_set_keyboard_handler(nas_tty_vt_key_event))
        nas_fail("virtual console keyboard handler could not register");
#endif
}
static bool controls_vt(void) {
    return vt.sid && nas_current_task && vt.sid==nas_current_task->sid;
}
void nas_vt_open_session(uint64_t flags) {
    struct nas_task *task=nas_current_task;
    /* Linux O_NOCTTY=0400 octal, carried unchanged through openat. Opening
     * an occupied VT still succeeds but does not steal its controlling SID. */
    if(!task || (flags&0x100U) || task->pid!=task->sid || vt.sid ||
       nas_tty_controlling() || nas_pty_controlling())return;
    vt.sid=task->sid;
    vt.pgid=task->pgid;
    reset_input();
    nas_fbcon_activate(true);
}
static uint64_t copy_pid(uint64_t arg,uint64_t pid) {
    uint32_t n=(uint32_t)pid;
    return nas_user_write_bytes(arg,(const uint8_t *)&n,sizeof(n))?
           0:(uint64_t)-EFAULT;
}
static uint64_t active_mode(void) {
    /* There is exactly one registered and physically active virtual console.
     * Other VT numbers must fail; pretending to switch would break the seat. */
    return 1U;
}
uint64_t nas_vt_ioctl(uint64_t cmd,uint64_t arg) {
    struct nas_task *task=nas_current_task;
    switch(cmd) {
    case 0x5600U: { /* VT_OPENQRY: no unused VT currently exists */
        const int32_t none=-1;
        return nas_user_write_bytes(arg,(const uint8_t *)&none,sizeof(none))?
            0:(uint64_t)-EFAULT;
    }
    case 0x5601U: /* VT_GETMODE */
        return nas_user_write_bytes(arg,(const uint8_t *)&vt.mode,sizeof(vt.mode))?
            0:(uint64_t)-EFAULT;
    case 0x5602U: { /* VT_SETMODE */
        struct nas_linux_vt_mode mode;
        if(!nas_user_read_bytes(arg,(uint8_t *)&mode,sizeof(mode)))
            return (uint64_t)-EFAULT;
        if(mode.mode!=NAS_VT_AUTO && mode.mode!=NAS_VT_PROCESS)
            return (uint64_t)-EINVAL;
        if(mode.mode==NAS_VT_PROCESS && (!mode.relsig || !mode.acqsig ||
                                        mode.relsig>64U || mode.acqsig>64U))
            return (uint64_t)-EINVAL;
        if(vt.mode_owner_pid && vt.mode_owner_pid!=task->pid)
            return (uint64_t)-EBUSY;
        vt.mode=mode;
        vt.mode_owner_pid=mode.mode==NAS_VT_PROCESS?task->pid:0;
        return 0;
    }
    case 0x5603U: { /* VT_GETSTATE */
        struct nas_linux_vt_stat state={(uint16_t)active_mode(),0U,2U};
        return nas_user_write_bytes(arg,(const uint8_t *)&state,sizeof(state))?
            0:(uint64_t)-EFAULT;
    }
    case 0x5605U: /* VT_RELDISP: only valid when a switch is pending */
        return (uint64_t)-EINVAL;
    case 0x5606U: /* VT_ACTIVATE */
    case 0x5607U: /* VT_WAITACTIVE */
        return arg==active_mode()?0:(uint64_t)-EINVAL;
    case 0x5608U: /* VT_DISALLOCATE: cannot remove the active VT */
        return (uint64_t)-EBUSY;
    case 0x4b3aU: /* KDSETMODE */
        if(arg!=NAS_KD_TEXT && arg!=NAS_KD_GRAPHICS)
            return (uint64_t)-EINVAL;
        if(vt.mode_owner_pid && vt.mode_owner_pid!=task->pid)
            return (uint64_t)-EBUSY;
        vt.kd_mode=(unsigned)arg;
        nas_fbcon_set_graphics(arg==NAS_KD_GRAPHICS);
        return 0;
    case 0x4b3bU: { /* KDGETMODE */
        int32_t mode=(int32_t)vt.kd_mode;
        return nas_user_write_bytes(arg,(const uint8_t *)&mode,sizeof(mode))?
            0:(uint64_t)-EFAULT;
    }
    case 0x4b44U: { /* KDGKBMODE */
        int32_t mode=(int32_t)vt.keyboard_mode;
        return nas_user_write_bytes(arg,(const uint8_t *)&mode,sizeof(mode))?
            0:(uint64_t)-EFAULT;
    }
    case 0x4b45U: /* KDSKBMODE: only supported translated key mode */
        if(arg!=1U)return (uint64_t)-EOPNOTSUPP;
        vt.keyboard_mode=(unsigned)arg;
        return 0;
    case 0x5401U: /* TCGETS */
        return nas_user_write_bytes(arg,(const uint8_t *)&vt.termios,36)?
            0:(uint64_t)-EFAULT;
    case 0x5402U:case 0x5403U:case 0x5404U: { /* TCSETS/W/F */
        struct nas_termios t;
        if(!nas_user_read_bytes(arg,(uint8_t *)&t,36))
            return (uint64_t)-EFAULT;
        if(t.line)return (uint64_t)-EINVAL;
        bool old_canon=(vt.termios.lflag&0000002U)!=0;
        vt.termios=t;
        vt.raw_owner_pid=(vt.termios.lflag&0000002U)?0:task->pid;
        if(old_canon && !(vt.termios.lflag&0000002U)) {
            vt.ldisc.ready=false;vt.ldisc.eof=false;
        }
        if(cmd==0x5404U)reset_input();
        return 0;
    }
    case 0x5413U: { /* TIOCGWINSZ */
        uint16_t ws[4]={vt.rows,vt.cols,vt.xpixel,vt.ypixel};
        return nas_user_write_bytes(arg,(const uint8_t *)ws,sizeof(ws))?
            0:(uint64_t)-EFAULT;
    }
    case 0x5414U: { /* TIOCSWINSZ */
        uint16_t ws[4];
        if(!nas_user_read_bytes(arg,(uint8_t *)ws,sizeof(ws)))
            return (uint64_t)-EFAULT;
        if(!ws[0]||!ws[1])return (uint64_t)-EINVAL;
        vt.rows=ws[0];vt.cols=ws[1];vt.xpixel=ws[2];vt.ypixel=ws[3];
        return 0;
    }
    case 0x540eU: { /* TIOCSCTTY: getty's O_NOCTTY + explicit claim path */
        if(task->pid!=task->sid)return (uint64_t)-EPERM;
        if(vt.sid && vt.sid!=task->sid && arg!=1U)
            return (uint64_t)-EPERM;
        bool new_owner=vt.sid!=task->sid;
        if(vt.sid && new_owner)reset_input();
        vt.sid=task->sid;vt.pgid=task->pgid;
        if(new_owner)nas_fbcon_activate(true);
        return 0;
    }
    case 0x5422U: /* TIOCNOTTY */
        if(!controls_vt())return (uint64_t)-ENOTTY;
        if(task->pid==task->sid)nas_vt_session_leader_exit(task->sid);
        return 0;
    case 0x5429U: /* TIOCGSID */
        return controls_vt()?copy_pid(arg,vt.sid):(uint64_t)-ENOTTY;
    case 0x540fU: /* TIOCGPGRP */
        return controls_vt()?copy_pid(arg,vt.pgid):(uint64_t)-ENOTTY;
    case 0x5410U: { /* TIOCSPGRP */
        if(!controls_vt())return (uint64_t)-ENOTTY;
        int32_t group;
        if(!nas_user_read_bytes(arg,(uint8_t *)&group,sizeof(group)))
            return (uint64_t)-EFAULT;
        if(group<=0)return (uint64_t)-EINVAL;
        for(unsigned i=0;i<NAS_TASK_LIMIT;i++) {
            struct nas_task *found=nas_task_at(i);
            if(found && found->state!=NAS_TASK_FREE &&
               found->sid==vt.sid && found->pgid==(uint32_t)group) {
                vt.pgid=(uint32_t)group;return 0;
            }
        }
        return (uint64_t)-EPERM;
    }
    default:return (uint64_t)-ENOTTY;
    }
}

void nas_vt_session_leader_exit(uint64_t sid) {
    if(!sid || vt.sid!=sid)return;
    vt.sid=vt.pgid=0;
    nas_fbcon_activate(false);
    reset_input();
    reset_graphics_ownership();
    default_termios();
}
void nas_vt_process_exit(uint64_t pid) {
    if(!pid)return;
    bool graphical=vt.mode_owner_pid==pid;
    bool raw=vt.raw_owner_pid==pid;
    if(graphical) {
        reset_graphics_ownership();
        nas_fbcon_set_graphics(false);
    }
    if(graphical || raw) {
        default_termios();
        reset_input();
    }
}

uint64_t nas_vt_write(const uint8_t *src,uint64_t length) {
    return nas_fbcon_write(src,length);
}

static uint64_t vt_budget(uint64_t requested,bool nonblock) {
    if(!requested)return 0;
#ifndef NAS_ENABLE_VIRTIO_INPUT
    (void)nonblock;
    return (uint64_t)-EOPNOTSUPP;
#else
    feed_input();
    if(vt.rx_overflow)return (uint64_t)-EOVERFLOW;
    unsigned wanted=requested>NAS_LDISC_CAPACITY?
                    NAS_LDISC_CAPACITY:(unsigned)requested;
    unsigned available=vt.ldisc.used-vt.ldisc.pos;
    bool expired=nas_current_task->wait.deadline_active &&
       nas_time_deadline_expired(nas_arch_monotonic_ns(),
                                 nas_current_task->wait.deadline_ns);
    if(!(vt.termios.lflag&0000002U))
        return nas_ldisc_noncanon_budget(&vt.termios,wanted,available,
                                         nonblock,expired);
    if(!nas_ldisc_readable(&vt.ldisc,&vt.termios,wanted))
        return (uint64_t)-EAGAIN;
    if(vt.ldisc.eof)return 0;
    return available<wanted?available:wanted;
#endif
}
uint64_t nas_vt_read_plan(uint64_t requested,bool nonblock) {
    return vt_budget(requested,nonblock);
}
uint64_t nas_vt_read(uint8_t *dst,uint64_t length,bool nonblock) {
    if(!length){
        if(vt.ldisc.eof&&vt.ldisc.ready)nas_ldisc_reset(&vt.ldisc);
        return 0;
    }
    if(!dst)return (uint64_t)-EFAULT;
    uint64_t budget=vt_budget(length,nonblock);
    if(vt.rx_overflow){vt.rx_overflow=false;return (uint64_t)-EOVERFLOW;}
    if((int64_t)budget<0)return budget;
    if(!budget){
        if(vt.ldisc.eof&&vt.ldisc.ready)nas_ldisc_reset(&vt.ldisc);
        return 0;
    }
    memcpy(dst,vt.ldisc.bytes+vt.ldisc.pos,(unsigned)budget);
    nas_ldisc_commit(&vt.ldisc,(unsigned)budget);
    feed_input();
    return budget;
}
bool nas_vt_read_waitable(void) {
#ifdef NAS_ENABLE_VIRTIO_INPUT
    return true;
#else
    return false;
#endif
}
void nas_vt_blocking_read_start(struct nas_task *task) {
    struct nas_ldisc_wait_clock clock={&task->wait.deadline_active,
        &task->wait.deadline_ns,&task->wait.tty_observed_bytes};
    nas_ldisc_noncanon_wait_start(&vt.termios,
         vt.ldisc.used-vt.ldisc.pos,clock,nas_arch_monotonic_ns());
}
bool nas_vt_read_ready(struct nas_task *task,uint64_t length) {
    if(!length || !nas_vt_read_waitable())return true;
    if(vt.pending_signum||vt.rx_overflow)return true;
    unsigned available=vt.ldisc.used-vt.ldisc.pos;
    if(vt.termios.lflag&0000002U)return vt.ldisc.ready;
    struct nas_ldisc_wait_clock clock={&task->wait.deadline_active,
        &task->wait.deadline_ns,&task->wait.tty_observed_bytes};
    return nas_ldisc_noncanon_wait_ready(&vt.termios,available,length,clock,
                                          nas_arch_monotonic_ns());
}
bool nas_vt_poll_readable(void) {
    if(!nas_vt_read_waitable())return false;
    if(vt.pending_signum||vt.rx_overflow)return true;
    if(vt.termios.lflag&0000002U)return vt.ldisc.ready;
    return nas_ldisc_noncanon_poll(&vt.termios,
                                   vt.ldisc.used-vt.ldisc.pos);
}
unsigned nas_vt_take_signal(uint64_t *sid,uint64_t *pgid) {
    unsigned signum=vt.pending_signum;
    if(signum) {
        *sid=vt.sid;*pgid=vt.pgid;
        vt.pending_signum=0;
    }
    return signum;
}

/* Linux EV_KEY -> decoded terminal key bytes, using the physical seat's
 * US keyboard layout for now. This shares the same N_TTY implementation
 * as serial and PTY rather than introducing a second terminal parser. */
static void push_byte(uint8_t b) {
    if(vt.count>=NAS_VT_QUEUE_CAP){vt.rx_overflow=true;return;}
    vt.rx[(vt.head+vt.count)%NAS_VT_QUEUE_CAP]=b;
    vt.count++;
}
static void emit_bytes(const char *seq) {
    for(;*seq;seq++)push_byte((uint8_t)*seq);
}
static void nas_vt_echo(void *context,uint8_t byte) {
    (void)context;
    (void)nas_fbcon_write(&byte,1);
}
static void feed_input(void) {
    while(vt.count && !vt.pending_signum) {
        uint8_t b=vt.rx[vt.head];
        if((vt.ldisc.ready || vt.ldisc.used==NAS_LDISC_CAPACITY) &&
            nas_ldisc_signal_event(&vt.termios,b)==NAS_LDISC_OK)break;
        vt.head=(vt.head+1U)%NAS_VT_QUEUE_CAP;
        vt.count--;
        enum nas_ldisc_event e=nas_ldisc_feed(&vt.ldisc,&vt.termios,b,
                                               nas_vt_echo,0);
        if(e!=NAS_LDISC_OK) {
            if(!(vt.termios.lflag&0000200U))reset_input();
            vt.pending_signum=e==NAS_LDISC_SIGNAL_INT?2U:
               e==NAS_LDISC_SIGNAL_QUIT?3U:20U;
            break;
        }
    }
}
static uint8_t translated_key(uint16_t code) {
    /* Linux input-event KEY_* codes: letters grouped by physical rows. */
    static const struct {uint16_t key; char chr;} letters[]={
        {16,'q'},{17,'w'},{18,'e'},{19,'r'},{20,'t'},{21,'y'},
        {22,'u'},{23,'i'},{24,'o'},{25,'p'},
        {30,'a'},{31,'s'},{32,'d'},{33,'f'},{34,'g'},{35,'h'},
        {36,'j'},{37,'k'},{38,'l'},
        {44,'z'},{45,'x'},{46,'c'},{47,'v'},{48,'b'},
        {49,'n'},{50,'m'}
    };
    for(unsigned i=0;i<sizeof(letters)/sizeof(letters[0]);i++)
        if(letters[i].key==code) {
            uint8_t c=(uint8_t)letters[i].chr;
            if(vt.ctrl)return (uint8_t)(c-'a'+1);
            if(vt.shift!=vt.caps)c=(uint8_t)(c-'a'+'A');
            return c;
        }
    if(code>=2U&&code<=11U){
        static const char digit[]="1234567890";
        static const char shifted[]="!@#$%^&*()";
        return (uint8_t)(vt.shift?shifted[code-2U]:digit[code-2U]);
    }
    switch(code){
    case 1:return 27U;                 /* Escape */
    case 14:return 127U;               /* Backspace */
    case 15:return 9U;                 /* Tab */
    case 28:return 10U;                /* Enter */
    case 57:return ' ';
    case 12:return vt.shift?'_':'-';
    case 13:return vt.shift?'+':'=';
    case 26:return vt.shift?'{':'[';
    case 27:return vt.shift?'}':']';
    case 39:return vt.shift?':':';';
    case 40:return vt.shift?'\"':'\'';
    case 41:return vt.shift?'~':'`';
    case 43:return vt.shift?'|':'\\';
    case 51:return vt.shift?'<':',';
    case 52:return vt.shift?'>':'.';
    case 53:return vt.shift?'?':'/';
    default:return 0;
    }
}
void nas_tty_vt_key_event(uint16_t code,bool pressed) {
    switch(code) {
    case 42:case 54:{ /* independent left/right shift state */
        uint8_t bit=code==42?1U:2U;
        if(pressed)vt.shift_bits|=bit;else vt.shift_bits&=(uint8_t)~bit;
        vt.shift=vt.shift_bits!=0;return;
    }
    case 29:case 97:{
        uint8_t bit=code==29?1U:2U;
        if(pressed)vt.ctrl_bits|=bit;else vt.ctrl_bits&=(uint8_t)~bit;
        vt.ctrl=vt.ctrl_bits!=0;return;
    }
    case 56:case 100:{
        uint8_t bit=code==56?1U:2U;
        if(pressed)vt.alt_bits|=bit;else vt.alt_bits&=(uint8_t)~bit;
        vt.alt=vt.alt_bits!=0;return;
    }
    case 58:
        if(pressed&&!vt.caps_down)vt.caps=!vt.caps;
        vt.caps_down=pressed;
        return;
    default:break;
    }
    if(!pressed)return;
    switch(code){
    case 103:emit_bytes("\x1b[A");break;
    case 108:emit_bytes("\x1b[B");break;
    case 106:emit_bytes("\x1b[C");break;
    case 105:emit_bytes("\x1b[D");break;
    case 102:emit_bytes("\x1b[H");break;
    case 107:emit_bytes("\x1b[F");break;
    case 104:emit_bytes("\x1b[5~");break;
    case 109:emit_bytes("\x1b[6~");break;
    case 110:emit_bytes("\x1b[2~");break;
    case 111:emit_bytes("\x1b[3~");break;
    default:{uint8_t c=translated_key(code);if(c)push_byte(c);break;}
    }
    feed_input();
}
