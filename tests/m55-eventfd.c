#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <poll.h>
#include <errno.h>

int main(void){
    int efd=eventfd(0,EFD_NONBLOCK|EFD_CLOEXEC);
    assert(efd>=0);
    int ep=epoll_create1(EPOLL_CLOEXEC);
    assert(ep>=0);
    struct epoll_event ev={.events=EPOLLIN,.data.u64=UINT64_C(0xfeed89abcdef1234)};
    assert(epoll_ctl(ep,EPOLL_CTL_ADD,efd,&ev)==0);
    struct epoll_event got[2]={{0}};
    assert(epoll_wait(ep,got,2,0)==0);
    uint64_t n=7,v=0;
    assert(write(efd,&n,8)==8);
    assert(epoll_wait(ep,got,2,150)==1);
    assert(got[0].events&EPOLLIN);
    assert(got[0].data.u64==ev.data.u64);
    struct pollfd pfd={.fd=efd,.events=POLLIN};
    assert(poll(&pfd,1,0)==1 && (pfd.revents&POLLIN));
    assert(read(efd,&v,8)==8 && v==7);
    assert(epoll_wait(ep,got,2,0)==0);
    assert(read(efd,&v,8)==-1 && errno==EAGAIN);
    n=UINT64_MAX;
    assert(write(efd,&n,8)==-1 && errno==EINVAL);
    assert(close(efd)==0);
    assert(close(ep)==0);
    int sem=eventfd(2,EFD_NONBLOCK|EFD_SEMAPHORE);
    assert(sem>=0);
    assert(read(sem,&v,8)==8 && v==1);
    assert(read(sem,&v,8)==8 && v==1);
    assert(read(sem,&v,8)==-1 && errno==EAGAIN);
    assert(close(sem)==0);
    puts("M55_NATIVE_EVENTFD_EPOLL_PASS");
    return 0;
}
