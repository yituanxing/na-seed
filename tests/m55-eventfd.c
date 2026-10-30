#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>

static void check_eventfd_vectors(void){
    /* Linux eventfd read_iter treats two four-byte destinations as one
     * eight-byte value. Its writev path instead applies each iovec as a
     * separate eventfd write; a leading zero-length segment is EINVAL. */
    int fd=eventfd(7,EFD_NONBLOCK);
    assert(fd>=0);
    uint32_t halves[2]={0,0};
    struct iovec split[2]={{&halves[0],4},{&halves[1],4}};
    assert(readv(fd,split,2)==8);
    uint64_t value=0;
    memcpy(&value,halves,sizeof(value));
    assert(value==7);
    assert(read(fd,&value,8)==-1 && errno==EAGAIN);
    uint64_t seven=7,eleven=11;
    struct iovec leading_zero[2]={{&seven,0},{&seven,8}};
    assert(writev(fd,leading_zero,2)==-1 && errno==EINVAL);
    assert(read(fd,&value,8)==-1 && errno==EAGAIN);
    struct iovec trailing_zero[2]={{&seven,8},{&seven,0}};
    assert(writev(fd,trailing_zero,2)==8);
    assert(read(fd,&value,8)==8 && value==7);
    struct iovec two_words[2]={{&seven,8},{&eleven,8}};
    assert(writev(fd,two_words,2)==16);
    assert(read(fd,&value,8)==8 && value==18);
    struct iovec short_words[2]={{&seven,4},{(uint8_t *)&seven+4,4}};
    assert(writev(fd,short_words,2)==-1 && errno==EINVAL);
    assert(close(fd)==0);
    puts("M55_NATIVE_EVENTFD_VECTOR_PASS");
}

static void check_pipe_writev_atomic(void){
    /* Discover usable pipe capacity using only ordinary nonblocking POSIX
     * calls. NaSeed currently supplies one 4096-byte PIPE_BUF buffer; Linux
     * host pipes may be bigger. No F_SETPIPE_SZ or target-specific ioctl. */
    int fds[2];
    assert(pipe2(fds,O_NONBLOCK)==0);
    char page[4096];
    memset(page,'X',sizeof(page));
    size_t capacity=0;
    for(;;){
        ssize_t n=write(fds[1],page,sizeof(page));
        if(n<0){assert(errno==EAGAIN);break;}
        assert(n==(ssize_t)sizeof(page));
        capacity+=(size_t)n;
        assert(capacity<=1024*1024);
    }
    assert(capacity>=4096 && capacity%4096==0);
    /* Begin from empty so a short tail occupies the final page's actual
     * free space, not a different ring-buffer slot. */
    for(size_t remaining=capacity;remaining;){
        ssize_t n=read(fds[0],page,remaining<sizeof(page)?remaining:sizeof(page));
        assert(n>0);
        remaining-=(size_t)n;
    }
    for(size_t remaining=capacity-96;remaining;){
        size_t chunk=remaining<sizeof(page)?remaining:sizeof(page);
        assert(write(fds[1],page,chunk)==(ssize_t)chunk);
        remaining-=chunk;
    }
    char first[50],second[100];
    memset(first,'A',sizeof(first));
    memset(second,'B',sizeof(second));
    struct iovec too_large[2]={{first,50},{second,100}};
    assert(writev(fds[1],too_large,2)==-1 && errno==EAGAIN);
    struct iovec fits[2]={{first,50},{second,40}};
    assert(writev(fds[1],fits,2)==90);
    const size_t total=capacity-6;
    for(size_t offset=0;offset<total;){
        size_t chunk=total-offset<sizeof(page)?total-offset:sizeof(page);
        ssize_t n=read(fds[0],page,chunk);
        assert(n>0);
        for(ssize_t i=0;i<n;i++){
            size_t position=offset+(size_t)i;
            char expected=position<capacity-96?'X':
                          position<capacity-46?'A':'B';
            assert(page[i]==expected);
        }
        offset+=(size_t)n;
    }
    char probe;
    assert(read(fds[0],&probe,1)==-1 && errno==EAGAIN);
    /* A later invalid source in one atomic vector cannot leak the first
     * valid slice into the pipe. */
    volatile uintptr_t invalid_address=1;
    struct iovec bad_source[2]={{first,2},{(void *)(uintptr_t)invalid_address,2}};
    assert(writev(fds[1],bad_source,2)==-1 && errno==EFAULT);
    assert(read(fds[0],&probe,1)==-1 && errno==EAGAIN);
    assert(close(fds[0])==0 && close(fds[1])==0);
    puts("M55_NATIVE_PIPE_WRITEV_ATOMIC_PASS");
}

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
    check_eventfd_vectors();
    check_pipe_writev_atomic();
    puts("M55_NATIVE_EVENTFD_EPOLL_PASS");
    return 0;
}
