#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#define P 4096U
static void fail(const char *what) {
    fprintf(stderr,"m11 VM error: %s errno=%d\n",what,errno);
    exit(1);
}
int main(void) {
    for (unsigned cycle=0;cycle<48;cycle++) {
        uint8_t *p=mmap(NULL,3*P,PROT_READ|PROT_WRITE,
                        MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if (p==MAP_FAILED) fail("initial mmap");
        p[0]=(uint8_t)cycle;
        p[P]=0x5a;
        p[2*P]=0xa5;
        if (munmap(p+P,P)) fail("middle munmap");
        uint8_t *hole=mmap(NULL,P,PROT_READ|PROT_WRITE,
                           MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if (hole==MAP_FAILED) fail("hole mmap");
        hole[0]=0x33;
        if (p[0]!=(uint8_t)cycle || p[2*P]!=0xa5) fail("neighbor");
        if (mprotect(hole,P,PROT_READ)) fail("mprotect R");
        if (hole[0]!=0x33) fail("protected read");
        if (mprotect(hole,P,PROT_READ|PROT_WRITE)) fail("mprotect RW");
        hole[0]=0x44;
        if (munmap(hole,P) || munmap(p,P) || munmap(p+2*P,P))
            fail("unmap pieces");
    }
    uint8_t *shared=mmap(NULL,P,PROT_READ|PROT_WRITE,
                         MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (shared==MAP_FAILED) fail("fork mmap");
    shared[0]=0x41;
    for (unsigned i=0;i<8;i++) {
        pid_t child=fork();
        if (child<0) fail("fork");
        if (!child) {
            if (shared[0]!=0x41) _exit(16);
            shared[0]=0x80;
            if (shared[0]!=0x80) _exit(17);
            _exit(0);
        }
        int status=-1;
        if (waitpid(child,&status,0)!=child || !WIFEXITED(status) ||
            WEXITSTATUS(status)!=0 || shared[0]!=0x41)
            fail("fork isolation/reap");
    }
    if (munmap(shared,P)) fail("last munmap");
    puts("m11-vm-ok");
    return 0;
}
