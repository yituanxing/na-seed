#include "tty_ldisc.h"
/* Standard Linux termios flag values and c_cc indices, not host libc. */
enum {
    IGNCR=0000200, ICRNL=0000400, INLCR=0000100,
    ISIG=0000001, ICANON=0000002, ECHO=0000010,
    VINTR=0, VQUIT=1, VERASE=2, VKILL=3, VEOF=4, VMIN=6,
    VSUSP=10, VWERASE=14
};
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
enum nas_ldisc_event nas_ldisc_feed(struct nas_ldisc *ld,const struct nas_termios *t,
                                   uint8_t b,nas_ldisc_echo_fn echo,void *context) {
    if(ld->ready||ld->used==NAS_LDISC_CAPACITY)return NAS_LDISC_OK;
    if(b=='\r') {
        if(t->iflag&IGNCR)return NAS_LDISC_OK;
        if(t->iflag&ICRNL)b='\n';
    } else if(b=='\n'&&(t->iflag&INLCR))b='\r';
    if(t->lflag&ISIG) {
        if(t->cc[VINTR]&&b==t->cc[VINTR])return NAS_LDISC_SIGNAL_INT;
        if(t->cc[VQUIT]&&b==t->cc[VQUIT])return NAS_LDISC_SIGNAL_QUIT;
        if(t->cc[VSUSP]&&b==t->cc[VSUSP])return NAS_LDISC_SIGNAL_STOP;
    }
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
