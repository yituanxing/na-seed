#include "tty_ldisc.h"
#include "../errno.h"
/* Standard Linux termios flag values and c_cc indices, not host libc. */
enum {
    IGNCR=0000200, ICRNL=0000400, INLCR=0000100,
    ISIG=0000001, ICANON=0000002, ECHO=0000010,
    VINTR=0, VQUIT=1, VERASE=2, VKILL=3, VEOF=4, VTIME=5, VMIN=6,
    VSUSP=10, VWERASE=14
};
static unsigned min_input(const struct nas_termios *termios,uint64_t requested) {
    unsigned minimum=termios->cc[VMIN];
    if(requested<minimum)minimum=(unsigned)requested;
    return minimum;
}
uint64_t nas_ldisc_noncanon_budget(const struct nas_termios *termios,
                                  uint64_t requested,unsigned available,
                                  bool nonblock,bool expired) {
    if(!requested)return 0;
    uint64_t count=available<requested?available:requested;
    if(nonblock)return count?count:(uint64_t)-EAGAIN;
    unsigned minimum=min_input(termios,requested);
    unsigned deciseconds=termios->cc[VTIME];
    if(!minimum && !deciseconds)return count;
    if(!minimum)return count?count:(expired?0:(uint64_t)-EAGAIN);
    if(count>=minimum)return count;
    if(deciseconds && expired && count)return count;
    return (uint64_t)-EAGAIN;
}
bool nas_ldisc_noncanon_poll(const struct nas_termios *termios,
                            unsigned available) {
    unsigned minimum=termios->cc[VMIN];
    return available >= (minimum?minimum:1U);
}
static uint64_t add_deciseconds(uint64_t now,unsigned deciseconds) {
    uint64_t delta=(uint64_t)deciseconds*100000000ULL;
    return UINT64_MAX-now<delta?UINT64_MAX:now+delta;
}
void nas_ldisc_noncanon_wait_start(const struct nas_termios *termios,
                                  unsigned available,
                                  struct nas_ldisc_wait_clock clock,
                                  uint64_t now_ns) {
    *clock.active=false;
    *clock.observed_bytes=0;
    if((termios->lflag&ICANON) || !termios->cc[VTIME])return;
    if(!termios->cc[VMIN] || available) {
        *clock.active=true;
        *clock.deadline_ns=add_deciseconds(now_ns,termios->cc[VTIME]);
        *clock.observed_bytes=available;
    }
}
bool nas_ldisc_noncanon_wait_ready(const struct nas_termios *termios,
                                  unsigned available,uint64_t requested,
                                  struct nas_ldisc_wait_clock clock,
                                  uint64_t now_ns) {
    if(!requested)return true;
    unsigned minimum=min_input(termios,requested);
    if(available >= (minimum?minimum:1U))return true;
    if(!termios->cc[VTIME])return false;
    if(minimum && !available) {
        /* A competing reader may have drained the first byte; an old
         * interbyte timer must not keep waking this Task indefinitely. */
        *clock.active=false;
        *clock.observed_bytes=0;
        return false;
    }
    if(!*clock.active || available>*clock.observed_bytes) {
        *clock.active=true;
        *clock.deadline_ns=add_deciseconds(now_ns,termios->cc[VTIME]);
        *clock.observed_bytes=available;
    }
    return now_ns>=*clock.deadline_ns;
}
void nas_ldisc_reset(struct nas_ldisc *ld) {
    ld->used=ld->pos=0; ld->ready=ld->eof=false;
}
static void emit(nas_ldisc_echo_fn e,void *context,uint8_t b) {
    if(e)e(context,b);
}
static void erase_echo(nas_ldisc_echo_fn e,void *context) {
    emit(e,context,'\b');emit(e,context,' ');emit(e,context,'\b');
}
static bool space(uint8_t b){return b==' '||b=='\t';}
enum nas_ldisc_event nas_ldisc_signal_event(const struct nas_termios *t,
                                            uint8_t b) {
    if(!(t->lflag&ISIG))return NAS_LDISC_OK;
    /* Linux N_TTY compares ISIG control bytes before translating CR/NL.
     * E.g. VINTR='\\n' with ICRNL does NOT turn a received CR into SIGINT. */
    if(t->cc[VINTR] && b==t->cc[VINTR])return NAS_LDISC_SIGNAL_INT;
    if(t->cc[VQUIT] && b==t->cc[VQUIT])return NAS_LDISC_SIGNAL_QUIT;
    if(t->cc[VSUSP] && b==t->cc[VSUSP])return NAS_LDISC_SIGNAL_STOP;
    return NAS_LDISC_OK;
}
enum nas_ldisc_event nas_ldisc_feed(struct nas_ldisc *ld,const struct nas_termios *t,
                                   uint8_t b,nas_ldisc_echo_fn echo,void *context) {
    enum nas_ldisc_event signal=nas_ldisc_signal_event(t,b);
    if(signal!=NAS_LDISC_OK)return signal;
    /* Signals bypass a completed canonical line or full input buffer.
     * Ordinary bytes still wait for the consumer to drain that buffer. */
    if(ld->ready||ld->used==NAS_LDISC_CAPACITY)return NAS_LDISC_OK;
    if(b=='\r') {
        if(t->iflag&IGNCR)return NAS_LDISC_OK;
        if(t->iflag&ICRNL)b='\n';
    } else if(b=='\n'&&(t->iflag&INLCR))b='\r';
    if(t->lflag&ICANON) {
        unsigned remove=0;
        bool edit=false;
        if(t->cc[VERASE]&&b==t->cc[VERASE]) {
            edit=true;remove=(ld->used>ld->pos);
        } else if(t->cc[VKILL]&&b==t->cc[VKILL]) {
            edit=true;remove=ld->used-ld->pos;
        } else if(t->cc[VWERASE]&&b==t->cc[VWERASE]) {
            edit=true;
            unsigned n=ld->used;
            while(n>ld->pos&&space(ld->bytes[n-1]))n--;
            while(n>ld->pos&&!space(ld->bytes[n-1]))n--;
            remove=ld->used-n;
        }
        if(edit) {
            ld->used-=remove;
            if(t->lflag&ECHO)while(remove--)erase_echo(echo,context);
            return NAS_LDISC_OK;
        }
        if(t->cc[VEOF]&&b==t->cc[VEOF]) {
            ld->eof=(ld->used==ld->pos);
            ld->ready=true;return NAS_LDISC_OK;
        }
    }
    ld->bytes[ld->used++]=b;
    if(t->lflag&ECHO) {
        if(b=='\n')emit(echo,context,'\r');
        emit(echo,context,b);
    }
    if((t->lflag&ICANON)&&(b=='\n'||ld->used==NAS_LDISC_CAPACITY))
        ld->ready=true;
    return NAS_LDISC_OK;
}
bool nas_ldisc_readable(const struct nas_ldisc *ld,const struct nas_termios *t,
                        unsigned wanted) {
    if(!wanted)return true;
    if(t->lflag&ICANON)return ld->ready;
    unsigned min=t->cc[VMIN];
    if(min>wanted)min=wanted;
    return ld->used-ld->pos>=min; /* VTIME handled by tty core */
}
unsigned nas_ldisc_peek(const struct nas_ldisc *ld,const struct nas_termios *t,
                        uint8_t *buffer,unsigned count) {
    if(!nas_ldisc_readable(ld,t,count)||ld->eof)return 0;
    unsigned avail=ld->used-ld->pos;
    if(count>avail)count=avail;
    for(unsigned i=0;i<count;i++)buffer[i]=ld->bytes[ld->pos+i];
    return count;
}
void nas_ldisc_commit(struct nas_ldisc *ld,unsigned count) {
    if(count>ld->used-ld->pos)count=ld->used-ld->pos;
    ld->pos+=count;
    if(ld->pos==ld->used)nas_ldisc_reset(ld);
}
