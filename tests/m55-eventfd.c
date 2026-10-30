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
    /* Deliberately invalid userspace address, kept runtime-volatile so
     * the host C compiler cannot fold read/write into a fortify warning. */
    volatile uintptr_t invalid_address=1;
    void *bad=(void *)(uintptr_t)invalid_address;
    assert(write(efd,bad,8)==-1 && errno==EFAULT);
    assert(write(efd,&n,7)==-1 && errno==EINVAL);
    assert(epoll_wait(ep,got,2,0)==0); /* failed write did not increment */
    assert(write(efd,&n,8)==8);
    assert(epoll_wait(ep,got,2,150)==1);
    assert(got[0].events&EPOLLIN);
    assert(got[0].data.u64==ev.data.u64);
    struct pollfd pfd={.fd=efd,.events=POLLIN};
    assert(poll(&pfd,1,0)==1 && (pfd.revents&POLLIN));
    assert(read(efd,&v,7)==-1 && errno==EINVAL);
    assert(read(efd,bad,8)==-1 && errno==EFAULT);
    /* Linux eventfd consumes its counter BEFORE a failed userspace copy.
     * This intentionally differs from pipe's EFAULT read atomicity. */
    assert(read(efd,&v,8)==-1 && errno==EAGAIN);
    assert(epoll_wait(ep,got,2,0)==0);
    /* An oversized read capacity returns exactly eight bytes without
     * overwriting the next word. An oversized WRITE instead gets EINVAL. */
    assert(write(efd,&n,8)==8);
    uint64_t words[2]={0,UINT64_C(0x6cf09a2b12345678)};
    assert(read(efd,words,sizeof(words))==8 && words[0]==7);
    assert(words[1]==UINT64_C(0x6cf09a2b12345678));
    assert(write(efd,words,sizeof(words))==-1 && errno==EINVAL);
    assert(epoll_wait(ep,got,2,0)==0);
    assert(epoll_wait(ep,got,2,0)==0);
    assert(read(efd,&v,8)==-1 && errno==EAGAIN);
    n=UINT64_MAX;
    assert(write(efd,&n,8)==-1 && errno==EINVAL);
    assert(close(efd)==0);
    assert(close(ep)==0);
    int sem=eventfd(2,EFD_NONBLOCK|EFD_SEMAPHORE);
    assert(sem>=0);
    assert(read(sem,bad,8)==-1 && errno==EFAULT);
    assert(read(sem,&v,8)==8 && v==1);
    assert(read(sem,&v,8)==-1 && errno==EAGAIN);
    assert(close(sem)==0);
    puts("M55_NATIVE_EVENTFD_EPOLL_PASS");
    return 0;
}
