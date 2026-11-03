/* M176 keeps the M174 PID1 ABI and desktop lifecycle, but first exercises
 * an UNMODIFIED Alpine Ncat binary over two genuinely discovered VirtIO
 * NICs. The only new native code is the acceptance orchestrator; packets
 * travel through the existing Linux socket ABI and upstream lwIP stack. */
#define _GNU_SOURCE
#define main m174_original_main
#include "../user/m174-devfs-launch.c"
#undef main
#include <arpa/inet.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>

static void wait_both_dhcp(void) {
    int fd=socket(AF_INET,SOCK_DGRAM,0);
    require(fd>=0,"open real lwIP interface-query socket");
    /* SIOCGIFCONF takes its address list from live lwIP DHCP leases. QEMU
     * virtio-mmio device addresses (and therefore eth0/eth1 enumeration)
     * need not follow the order of -device arguments. Require exactly two
     * named, independently leased NICs and both real guest subnets, without
     * incorrectly assigning either subnet to a particular interface name. */
    int ready=0;
    for(unsigned attempt=0;attempt<120U && !ready;attempt++) {
        struct ifreq entries[8]={0};
        struct ifconf conf={.ifc_len=sizeof(entries),.ifc_req=entries};
        const int query=ioctl(fd,SIOCGIFCONF,&conf);
        if(query==0 &&
           conf.ifc_len>=0 && conf.ifc_len<=(int)sizeof(entries) &&
           conf.ifc_len%(int)sizeof(struct ifreq)==0) {
            unsigned eth0=0,eth1=0,subnet2=0,subnet3=0;
            for(unsigned i=0;i<(unsigned)conf.ifc_len/sizeof(entries[0]);i++) {
                const uint32_t ip=ntohl(((struct sockaddr_in *)
                    (void *)&entries[i].ifr_addr)->sin_addr.s_addr);
                if(strcmp(entries[i].ifr_name,"eth0") &&
                   strcmp(entries[i].ifr_name,"eth1"))continue;
                if(!strcmp(entries[i].ifr_name,"eth0"))eth0++;
                else eth1++;
                if((ip>>8)==UINT32_C(0x0a0002))subnet2++;
                if((ip>>8)==UINT32_C(0x0a0003))subnet3++;
            }
            ready=eth0==1 && eth1==1 && subnet2==1 && subnet3==1;
            if(ready) {
                printf("[M176 DUAL DHCP] eth0+eth1 leased both "
                       "10.0.2/24 and 10.0.3/24 (enumeration independent)\n");
                fflush(stdout);
            }
        }
        if(!ready && attempt==119U) {
            printf("[M176 DHCP DIAG] ioctl=%d errno=%d len=%d "
                   "ifreq_size=%zu\n",query,errno,conf.ifc_len,
                   sizeof(struct ifreq));
            if(query==0 && conf.ifc_len>=0 &&
               conf.ifc_len<=(int)sizeof(entries) &&
               conf.ifc_len%(int)sizeof(struct ifreq)==0) {
                for(unsigned i=0;i<(unsigned)conf.ifc_len/sizeof(entries[0]);i++) {
                    const uint32_t ip=ntohl(((struct sockaddr_in *)
                        (void *)&entries[i].ifr_addr)->sin_addr.s_addr);
                    printf("[M176 DHCP DIAG] if=%s ip=%u.%u.%u.%u\n",
                           entries[i].ifr_name,(unsigned)(ip>>24),
                           (unsigned)((ip>>16)&255U),
                           (unsigned)((ip>>8)&255U),(unsigned)(ip&255U));
                }
            }
            fflush(stdout);
        }
        if(ready)break;
        struct timespec delay={.tv_sec=0,.tv_nsec=250000000L};
        while(nanosleep(&delay,&delay)<0 && errno==EINTR){}
    }
    require(close(fd)==0,"close live interface-query socket");
    require(ready,"both independent VirtIO netif DHCP IPv4 leases");
    puts("M176_DUAL_REAL_DHCP_READY_PASS");fflush(stdout);
}

static void ncat_http(void) {
    wait_both_dhcp();
    int to_child[2],from_child[2];
    require(pipe(to_child)==0 && pipe(from_child)==0,"Ncat IPC pipes");
    pid_t pid=fork();
    require(pid>=0,"fork unmodified Alpine Ncat");
    if(pid==0) {
        if(dup2(to_child[0],STDIN_FILENO)<0 ||
           dup2(from_child[1],STDOUT_FILENO)<0)_exit(120);
        close(to_child[0]);close(to_child[1]);
        close(from_child[0]);close(from_child[1]);
        char *const args[]={(char *)"/usr/bin/ncat",(char *)"-w",
            (char *)"12",(char *)"10.0.2.2",(char *)"15416",NULL};
        execv(args[0],args);
        _exit(121);
    }
    close(to_child[0]);close(from_child[1]);
    static const char request[]=
        "GET /m176-ncat-irq HTTP/1.0\r\nHost: m176.naseed.test\r\n"
        "Connection: close\r\n\r\n";
    const char *cursor=request;
    size_t left=sizeof(request)-1U;
    while(left) {
        ssize_t n=write(to_child[1],cursor,left);
        require(n>0 || (n<0 && errno==EINTR),"write exact Ncat HTTP GET");
        if(n<0)continue;
        cursor+=n;left-=(size_t)n;
    }
    require(close(to_child[1])==0,"close Ncat request producer");
    char response[4096]={0};
    size_t total=0;
    for(;;) {
        struct pollfd ready={.fd=from_child[0],.events=POLLIN|POLLHUP};
        int result=poll(&ready,1,20000);
        require(result>0 || (result<0 && errno==EINTR),
                "Ncat HTTP response bounded readiness");
        if(result<0)continue;
        ssize_t n=read(from_child[0],response+total,
                       sizeof(response)-1U-total);
        require(n>=0 || errno==EINTR,"read original Ncat HTTP response");
        if(n<0)continue;
        if(!n)break;
        total+=(size_t)n;
        require(total<sizeof(response)-1U,"Ncat response size bounded");
    }
    require(close(from_child[0])==0,"close Ncat reply pipe");
    int status=0;
    require(waitpid(pid,&status,0)==pid && WIFEXITED(status) &&
            WEXITSTATUS(status)==0,"original Alpine Ncat exited cleanly");
    require(strstr(response,"HTTP/1.0 200 OK")!=NULL &&
            strstr(response,"M176_REAL_NCAT_HTTP_RESPONSE")!=NULL,
            "Ncat received exact host HTTP response");
    printf("M176_ORIGINAL_ALPINE_NCAT_HTTP_PASS bytes=%zu\n",total);
    fflush(stdout);
}

#ifdef NAS_M180_FS_USER_STAGE
/* Only the opt-in M180 probe runs this separately linked Linux program.
 * The original M176/M174 native PID1 path and all existing Alpine/GPU
 * acceptance continue afterwards, in the same QEMU guest. */
static void m180_user_fs(void) {
    pid_t child=fork();
    require(child>=0,"spawn ordinary user fsync test Task");
    if(child==0) {
        char *const argv[]={(char *)"/usr/bin/naseed-m180-fs-user",NULL};
        execv(argv[0],argv);
        _exit(180);
    }
    int status=0;
    require(waitpid(child,&status,0)==child && WIFEXITED(status) &&
            WEXITSTATUS(status)==0,
            "ordinary Linux ELF openat/read/write/fsync/close child exited 0");
    puts("[M180 USER FS LAUNCH PASS]");
    fflush(stdout);
}
#endif

int main(void) {
    ncat_http();
#ifdef NAS_M180_FS_USER_STAGE
    m180_user_fs();
#endif
    /* Preserves the original M174 devfs/20-ELF/gui PID1, so QMP still
     * exercises five physical screen states and shell-launched fifth LVGL. */
    return m174_original_main();
}
