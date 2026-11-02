#define _GNU_SOURCE
#include "protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* Shared userspace IPC/anonymous surface ownership. No kernel-specific ABI
 * beyond the same Linux AF_UNIX, SCM_RIGHTS, mmap and memfd calls. */
void nasd_fail(const char *detail) {
    fprintf(stderr,"[M167 APP FAIL] %s errno=%d\n",detail,errno);
    fflush(stderr);
    exit(17);
}
void nasd_mark(const char *value) {puts(value);fflush(stdout);}
void nasd_require(int ok,const char *message) {if(!ok)nasd_fail(message);}
void nasd_write_all(int fd,const void *data,size_t length) {
    const uint8_t *src=data;
    while(length) {
        ssize_t n=write(fd,src,length);
        if(n<=0) {if(n<0&&errno==EINTR)continue;nasd_fail("socket/pty write");}
        src+=(size_t)n;length-=(size_t)n;
    }
}
void nasd_read_all(int fd,void *data,size_t length) {
    uint8_t *dst=data;
    while(length) {
        ssize_t n=read(fd,dst,length);
        if(n<=0) {if(n<0&&errno==EINTR)continue;nasd_fail("socket/file read");}
        dst+=(size_t)n;length-=(size_t)n;
    }
}
void nasd_send_packet(int fd,uint32_t type,uint32_t key) {
    const struct message m={.type=type,.key=key};
    nasd_write_all(fd,&m,sizeof(m));
}
struct message nasd_recv_packet(int fd) {
    struct message m={0};nasd_read_all(fd,&m,sizeof(m));
    nasd_require(!m.reserved && !m.value,"desktop IPC reserved fields");
    return m;
}
void nasd_send_surface(int fd,int surface,uint32_t role,unsigned width,
                       unsigned height) {
    nasd_require(width>0 && height>0 && width<=SCREEN_W &&
                 height<=SCREEN_H-TITLE_H,
                 "client surface dimensions in screen bounds");
    struct message packet={.type=MSG_READY,.key=role,
                           .value=(width<<16U)|height};
    struct iovec iov={.iov_base=&packet,.iov_len=sizeof(packet)};
    union {struct cmsghdr align;uint8_t bytes[CMSG_SPACE(sizeof(int))];} control={0};
    struct msghdr m={.msg_iov=&iov,.msg_iovlen=1,
                     .msg_control=control.bytes,.msg_controllen=sizeof(control.bytes)};
    struct cmsghdr *c=CMSG_FIRSTHDR(&m);
    nasd_require(c!=NULL,"SCM_RIGHTS cmsg");
    c->cmsg_level=SOL_SOCKET;c->cmsg_type=SCM_RIGHTS;
    c->cmsg_len=CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(c),&surface,sizeof(surface));
    nasd_require(sendmsg(fd,&m,0)==(ssize_t)sizeof(packet),
            "sendmsg memfd SCM_RIGHTS");
}
int nasd_receive_surface(int fd,int role,unsigned *width,unsigned *height) {
    nasd_require(width!=NULL && height!=NULL,
                 "surface metadata outputs");
    /* A client does real work (ImageMagick or PTY shell startup) before
     * announcing its memfd. Park on socket readiness before recvmsg: NaSeed
     * currently exposes its wait/retry scheduler for poll and read/write,
     * while an empty blocking recvmsg still returns EOPNOTSUPP. This also
     * fits a compositor's eventual poll-driven multi-client event loop. */
    struct pollfd ready={.fd=fd,.events=POLLIN};
    for(;;) {
        int n=poll(&ready,1,120000);
        if(n<0 && errno==EINTR)continue;
        nasd_require(n==1 && (ready.revents&POLLIN) &&
                !(ready.revents&(POLLERR|POLLNVAL)),
                "wait for independent image/terminal surface readiness");
        break;
    }
    struct message packet={0};
    struct iovec iov={.iov_base=&packet,.iov_len=sizeof(packet)};
    union {struct cmsghdr align;uint8_t bytes[CMSG_SPACE(sizeof(int))];} control={0};
    struct msghdr m={.msg_iov=&iov,.msg_iovlen=1,
                     .msg_control=control.bytes,.msg_controllen=sizeof(control.bytes)};
    ssize_t n=recvmsg(fd,&m,MSG_CMSG_CLOEXEC);
    nasd_require(n>0 && n<=(ssize_t)sizeof(packet),"recvmsg surface header");
    if(n<(ssize_t)sizeof(packet))
        nasd_read_all(fd,(uint8_t *)&packet+n,sizeof(packet)-(size_t)n);
    const unsigned w=packet.value>>16U,h=packet.value&65535U;
    nasd_require(packet.type==MSG_READY && packet.key==(unsigned)role &&
            w>0 && h>0 && w<=SCREEN_W && h<=SCREEN_H-TITLE_H &&
            !packet.reserved && !(m.msg_flags&MSG_CTRUNC),
            "surface role/protocol mismatch");
    struct cmsghdr *c=CMSG_FIRSTHDR(&m);
    nasd_require(c!=NULL && c->cmsg_level==SOL_SOCKET && c->cmsg_type==SCM_RIGHTS &&
            c->cmsg_len==CMSG_LEN(sizeof(int)),"real received memfd capability");
    int shared=-1;memcpy(&shared,CMSG_DATA(c),sizeof(shared));
    nasd_require(shared>=0,"invalid received memfd");
    *width=w;*height=h;
    return shared;
}
uint8_t *nasd_make_surface(const char *name,int w,int h,int *outfd) {
    size_t length=(size_t)w*(size_t)h*4U;
    int fd=memfd_create(name,MFD_CLOEXEC);
    nasd_require(fd>=0,"create real anonymous tmpfs memfd");
    nasd_require(ftruncate(fd,(off_t)length)==0,"resize shared memfd surface");
    void *map=mmap(NULL,length,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    nasd_require(map!=MAP_FAILED,"MAP_SHARED client surface");
    *outfd=fd;
    return map;
}
void nasd_fill_surface(uint8_t *data,int w,int h,
                         uint8_t r,uint8_t g,uint8_t b) {
    for(size_t p=0;p<(size_t)w*(size_t)h;p++) {
        data[4*p+0]=b;data[4*p+1]=g;data[4*p+2]=r;data[4*p+3]=255;
    }
}
