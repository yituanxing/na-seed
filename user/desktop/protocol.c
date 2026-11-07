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
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
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
void nasd_send_key(int fd,uint32_t key,uint32_t modifiers) {
    nasd_require(!(modifiers&~NASD_MOD_MASK),"bounded keyboard modifiers");
    const struct message m={.type=MSG_KEY,.key=key,.value=modifiers};
    nasd_write_all(fd,&m,sizeof(m));
}
void nasd_send_pointer(int fd,unsigned x,unsigned y,int pressed) {
    nasd_require(x<SCREEN_W && y<SCREEN_H &&
                 (pressed==0 || pressed==1),"pointer packet bounded");
    const struct message m={.type=MSG_POINTER,.key=(uint32_t)pressed,
                            .value=(x<<16U)|y};
    nasd_write_all(fd,&m,sizeof(m));
}
void nasd_send_scroll(int fd,int vertical_120,int horizontal_120) {
    nasd_require(vertical_120>=-12000 && vertical_120<=12000 &&
                 horizontal_120>=-12000 && horizontal_120<=12000,
                 "bounded high-resolution scroll delta");
    const struct message m={.type=MSG_SCROLL,.key=(uint32_t)vertical_120,
                            .value=(uint32_t)horizontal_120};
    nasd_write_all(fd,&m,sizeof(m));
}
void nasd_send_pointer_leave(int fd) {
    nasd_send_packet(fd,MSG_POINTER_LEAVE,0);
}
struct message nasd_recv_packet(int fd) {
    struct message m={0};nasd_read_all(fd,&m,sizeof(m));
    nasd_require(!m.reserved,"desktop IPC reserved bits");
    if(m.type==MSG_POINTER) {
        nasd_require(m.key<=1 && (m.value>>16U)<SCREEN_W &&
                     (m.value&65535U)<SCREEN_H,
                     "bounded client pointer packet");
    } else if(m.type==MSG_KEY)
        nasd_require(!(m.value&~NASD_MOD_MASK),
                     "desktop keyboard modifier encoding");
    else if(m.type==MSG_SCROLL)
        nasd_require((int32_t)m.key>=-12000 && (int32_t)m.key<=12000 &&
                     (int32_t)m.value>=-12000 && (int32_t)m.value<=12000,
                     "bounded signed high-resolution scroll packet");
    else if(m.type==MSG_POINTER_LEAVE)
        nasd_require(!m.key && !m.value,
                     "pointer leave has no stale coordinates or buttons");
    else nasd_require(!m.value,"desktop IPC reserved fields");
    return m;
}
static int surface_caps_valid(uint32_t caps) {
    const uint32_t valid=NASD_INPUT_KEYS|NASD_INPUT_POINTER|
                         NASD_INPUT_SCROLL|NASD_INPUT_POINTER_MOTION;
    return !(caps&~valid) &&
           (!(caps&(NASD_INPUT_SCROLL|NASD_INPUT_POINTER_MOTION)) ||
            (caps&NASD_INPUT_POINTER));
}
void nasd_send_surface_caps(int fd,int surface,uint32_t role,unsigned width,
                            unsigned height,uint32_t capabilities) {
    nasd_require(width>0 && height>0 && width<=SCREEN_W &&
                 height<=SCREEN_H-TITLE_H,
                 "client surface dimensions in screen bounds");
    nasd_require(surface_caps_valid(capabilities),
                 "desktop input capabilities");
    struct message packet={.type=MSG_READY,.key=role,
                           .value=(width<<16U)|height,
                           .reserved=getenv(NASD_PROTOCOL_ENV)?capabilities:0};
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
void nasd_send_surface(int fd,int surface,uint32_t role,unsigned width,
                       unsigned height) {
    nasd_send_surface_caps(fd,surface,role,width,height,0);
}
static int surface_valid(const struct message *packet,int shared,
                         uint32_t *kind,unsigned *width,unsigned *height,
                         uint32_t *capabilities) {
    const unsigned w=packet->value>>16U,h=packet->value&65535U;
    if(packet->type!=MSG_READY ||
       !surface_caps_valid(packet->reserved) ||
       !w || !h || w>SCREEN_W || h>SCREEN_H-TITLE_H || shared<0)
        return 0;
    struct stat backing={0};
    const uint64_t expected=(uint64_t)w*(uint64_t)h*4U;
    if(fstat(shared,&backing)!=0 || !S_ISREG(backing.st_mode) ||
       backing.st_size<0 || (uint64_t)backing.st_size!=expected)
        return 0;
    if(kind)*kind=packet->key;
    if(capabilities)*capabilities=packet->reserved;
    if(width)*width=w;
    if(height)*height=h;
    return 1;
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
    nasd_require(packet.key==(unsigned)role &&
            !(m.msg_flags&(MSG_CTRUNC|MSG_TRUNC)),
            "surface role/protocol mismatch");
    struct cmsghdr *c=CMSG_FIRSTHDR(&m);
    nasd_require(c!=NULL && c->cmsg_level==SOL_SOCKET && c->cmsg_type==SCM_RIGHTS &&
            c->cmsg_len==CMSG_LEN(sizeof(int)),"real received memfd capability");
    int shared=-1;memcpy(&shared,CMSG_DATA(c),sizeof(shared));
    nasd_require(shared>=0,"invalid received memfd");
    /* Do not trust the surface dimensions or the sender's FD type merely
     * because the SCM_RIGHTS message is syntactically valid. mmap() may
     * succeed on an undersized file, yet pixel reads would SIGBUS. The
     * bounded protocol specifies one exact XRGB32 allocation per window. */
    nasd_require(!packet.reserved &&
                 surface_valid(&packet,shared,NULL,width,height,NULL),
                 "regular surface FD with exact advertised byte length");
    return shared;
}
void nasd_surface_offer_init(struct nasd_surface_offer *offer) {
    *offer=(struct nasd_surface_offer){.surface_fd=-1};
}
void nasd_surface_offer_release(struct nasd_surface_offer *offer) {
    if(offer->surface_fd>=0)close(offer->surface_fd);
    nasd_surface_offer_init(offer);
}
int nasd_surface_offer_step(int channel,struct nasd_surface_offer *offer,
                            uint32_t *kind,unsigned *width,unsigned *height,
                            uint32_t *capabilities) {
    if(offer->received==sizeof(offer->packet))return -1;
    const size_t remaining=sizeof(offer->packet)-offer->received;
    ssize_t n;
    if(!offer->received) {
        struct iovec iov={.iov_base=&offer->packet,.iov_len=remaining};
        union {struct cmsghdr align;
               uint8_t bytes[CMSG_SPACE(sizeof(int))];} control={0};
        struct msghdr m={.msg_iov=&iov,.msg_iovlen=1,
             .msg_control=control.bytes,.msg_controllen=sizeof(control.bytes)};
        n=recvmsg(channel,&m,MSG_CMSG_CLOEXEC|MSG_DONTWAIT);
        if(n>0) {
            struct cmsghdr *c=CMSG_FIRSTHDR(&m);
            int delivered=-1;
            if(c && c->cmsg_level==SOL_SOCKET && c->cmsg_type==SCM_RIGHTS &&
               c->cmsg_len>=CMSG_LEN(sizeof(int)))
                memcpy(&delivered,CMSG_DATA(c),sizeof(int));
            if(!c || c->cmsg_level!=SOL_SOCKET || c->cmsg_type!=SCM_RIGHTS ||
               c->cmsg_len!=CMSG_LEN(sizeof(int)) ||
               (m.msg_flags&(MSG_CTRUNC|MSG_TRUNC)) || delivered<0) {
                if(delivered>=0)close(delivered);
                return -1;
            }
            offer->surface_fd=delivered;
        }
    } else {
        n=read(channel,(uint8_t *)&offer->packet+offer->received,remaining);
    }
    if(n<0 && (errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK))return 0;
    if(n<=0 || (size_t)n>remaining)return -1;
    offer->received+=(unsigned)n;
    if(offer->received<sizeof(offer->packet))return 0;
    return surface_valid(&offer->packet,offer->surface_fd,
                         kind,width,height,capabilities) ? 1:-1;
}
int nasd_packet_step(int channel,struct nasd_packet_reader *reader,
                     struct message *result) {
    const size_t remaining=sizeof(reader->packet)-reader->received;
    if(!remaining)return -1;
    ssize_t n=read(channel,(uint8_t *)&reader->packet+reader->received,
                   remaining);
    if(n<0 && (errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK))return 0;
    if(n<=0 || (size_t)n>remaining)return -1;
    reader->received+=(unsigned)n;
    if(reader->received<sizeof(reader->packet))return 0;
    *result=reader->packet;
    *reader=(struct nasd_packet_reader){0};
    /* Application-to-compositor data plane never accepts peer-generated
     * input events, or arbitrary values in a repaint/close notification. */
    if(result->reserved || (result->type!=MSG_UPDATED &&
                            result->type!=MSG_CLOSE) ||
       (result->type==MSG_CLOSE && (result->key || result->value)) ||
       result->value)return -1;
    return 1;
}
int nasd_client_channel(void) {
    /* Compositor-launched legacy and bootstrap clients receive a connected
     * descriptor 3. A child started later by BusyBox shell has no fd 3 and
     * connects to exactly the same ordinary AF_UNIX server as any app. */
    if(!getenv(NASD_PROTOCOL_ENV))return 3; /* M167-M171 exact old ABI */
    const char *inherited=getenv(NASD_INHERITED_ENV);
    if(inherited && !strcmp(inherited,"3")) {
        nasd_require(fcntl(3,F_GETFD)>=0,
                     "compositor inherited FD3 capability missing");
        return 3;
    }
    const char *path=getenv(NASD_SOCKET_ENV);
    if(!path || !*path)path=NASD_SOCKET_PATH;
    nasd_require(strlen(path)<sizeof(((struct sockaddr_un *)0)->sun_path),
                 "desktop socket path length");
    int fd=socket(AF_UNIX,SOCK_STREAM,0);
    nasd_require(fd>=0,"create external desktop client socket");
    struct sockaddr_un address={.sun_family=AF_UNIX};
    strcpy(address.sun_path,path);
    nasd_require(connect(fd,(const struct sockaddr *)&address,
                          sizeof(address))==0,
                 "connect external desktop session");
    return fd;
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
