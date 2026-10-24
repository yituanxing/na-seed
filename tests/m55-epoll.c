#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>

/* Entirely normal Linux epoll/pipe semantics compiled and run by the
 * original target GCC in the NaSeed guest. Not a kernel-aware test hook. */
int main(void){
    int ep=epoll_create1(EPOLL_CLOEXEC);
    assert(ep>=0);
    int pipefds[2]={-1,-1};
    assert(pipe(pipefds)==0);
    struct epoll_event ev={.events=EPOLLIN,.data.u64=UINT64_C(0x1234567812345678)};
    assert(epoll_ctl(ep,EPOLL_CTL_ADD,pipefds[0],&ev)==0);
    struct epoll_event got[2];
    assert(epoll_wait(ep,got,2,0)==0);
    assert(write(pipefds[1],"X",1)==1);
    assert(epoll_wait(ep,got,2,500)==1);
    assert((got[0].events&EPOLLIN)!=0);
    assert(got[0].data.u64==UINT64_C(0x1234567812345678));
    char byte=0;
    assert(read(pipefds[0],&byte,1)==1 && byte=='X');
    assert(epoll_wait(ep,got,2,0)==0);
    ev.data.u64=UINT64_C(0xabcdef0123456789);
    assert(epoll_ctl(ep,EPOLL_CTL_MOD,pipefds[0],&ev)==0);
    assert(write(pipefds[1],"Y",1)==1);
    assert(epoll_wait(ep,got,2,100)==1);
    assert(got[0].data.u64==UINT64_C(0xabcdef0123456789));
    assert(epoll_ctl(ep,EPOLL_CTL_DEL,pipefds[0],NULL)==0);
    assert(epoll_wait(ep,got,2,0)==0);
    assert(close(pipefds[0])==0);
    assert(close(pipefds[1])==0);
    assert(close(ep)==0);
    puts("M55_NATIVE_EPOLL_PIPE_PASS");
    return 0;
}
