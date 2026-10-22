#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kernel/drivers/tty_ldisc.h"
#define CK(x) do { if(!(x)){fprintf(stderr,"ldisc fail line %d: %s\n",__LINE__,#x);exit(1);} }while(0)
static unsigned echoes;
static void echo_byte(void *ctx,uint8_t b){(void)ctx;(void)b;echoes++;}
static struct nas_termios config(void) {
    struct nas_termios t={0};t.iflag=0000400;t.lflag=0000013;
    t.cc[0]=3;t.cc[1]=28;t.cc[2]=127;t.cc[3]=21;
    t.cc[4]=4;t.cc[6]=1;t.cc[10]=26;t.cc[14]=23;return t;
}
static void feed(struct nas_ldisc *l,const struct nas_termios *t,const char *s) {
    while(*s)CK(nas_ldisc_feed(l,t,(uint8_t)*s++,echo_byte,0)==NAS_LDISC_OK);
}
static void expect(struct nas_ldisc *l,struct nas_termios *t,const char *s,unsigned n) {
    uint8_t buf[256];CK(nas_ldisc_readable(l,t,n));
    CK(nas_ldisc_peek(l,t,buf,n)==n);
    for(unsigned i=0;i<n;i++)CK(buf[i]==(uint8_t)s[i]);
    nas_ldisc_commit(l,n);
}
int main(void) {
    struct nas_ldisc ld;struct nas_termios t=config();nas_ldisc_reset(&ld);
    feed(&ld,&t,"abc\r");expect(&ld,&t,"ab",2);expect(&ld,&t,"c\n",2);
    feed(&ld,&t,"abx\177c\n");expect(&ld,&t,"abc\n",4);
    feed(&ld,&t,"abc\025clean\n");expect(&ld,&t,"clean\n",6);
    feed(&ld,&t,"echo wrong\027right\n");expect(&ld,&t,"echo right\n",11);
    CK(echoes>0);echoes=0;t.lflag &= ~0000010;
    feed(&ld,&t,"noecho\n");CK(echoes==0);expect(&ld,&t,"noecho\n",7);
    feed(&ld,&t,"\004");CK(ld.eof&&ld.ready);
    uint8_t buf[16];CK(nas_ldisc_peek(&ld,&t,buf,16)==0);
    nas_ldisc_reset(&ld);feed(&ld,&t,"hi\004");expect(&ld,&t,"hi",2);
    t.lflag &= ~0000002; t.cc[6]=1;
    feed(&ld,&t,"z");expect(&ld,&t,"z",1);
    feed(&ld,&t,"xy");t.cc[6]=2;expect(&ld,&t,"xy",2);
    t.cc[6]=0;CK(nas_ldisc_readable(&ld,&t,16));
    CK(nas_ldisc_feed(&ld,&t,3,echo_byte,0)==NAS_LDISC_SIGNAL_INT);
    CK(nas_ldisc_feed(&ld,&t,26,echo_byte,0)==NAS_LDISC_SIGNAL_STOP);
    CK(nas_ldisc_feed(&ld,&t,28,echo_byte,0)==NAS_LDISC_SIGNAL_QUIT);
    t.lflag &= ~0000001;
    CK(nas_ldisc_feed(&ld,&t,3,echo_byte,0)==NAS_LDISC_OK);
    puts("[PASS] N_TTY canonical/edit/echo/EOF/noncanonical/signal");
    return 0;
}
