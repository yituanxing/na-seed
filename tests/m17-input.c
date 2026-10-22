#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
/* Standalone real Linux terminal input program, not a fake kernel applet.
 * Boot as the actual PID 1 in QEMU; acquire the console and read a host
 * byte stream sent AFTER the READY line over the same COM1 hardware. */
int main(void) {
    if(ioctl(0,TIOCSCTTY,0)!=0) {
        fprintf(stderr,"m17 TIOCSCTTY errno=%d\n",errno);
        return 19;
    }
    struct termios t;
    if(tcgetattr(0,&t)!=0)return 20;
    t.c_lflag|=ICANON|ECHO;
    t.c_iflag|=ICRNL;
    if(tcsetattr(0,TCSANOW,&t)!=0)return 21;
    puts("[M17] tty-input-ready");
    fflush(stdout);
    char b[24];
    ssize_t n=read(0,b,sizeof b);
    if(n!=6 || memcmp(b,"hello\n",6)!=0) {
        fprintf(stderr,"m17 input mismatch size=%ld\n",(long)n);
        return 22;
    }
    puts("m17-input-ok");
    return 0;
}
