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
    /* The actual SIOCGIFCONF implementation takes its address list from
     * lwIP's live netif leases. No fixed IP is ever asserted just because
     * a QEMU NIC appeared or a timer expired. */
    int ready=0;
    for(unsigned attempt=0;attempt<120U && !ready;attempt++) {
        struct ifreq entries[8]={0};
        struct ifconf conf={.ifc_len=sizeof(entries),.ifc_req=entries};
        if(ioctl(fd,SIOCGIFCONF,&conf)==0 &&
           conf.ifc_len>=0 && conf.ifc_len<=(int)sizeof(entries) &&
           conf.ifc_len%(int)sizeof(struct ifreq)==0) {
            int first=0,second=0;
            for(unsigned i=0;i<(unsigned)conf.ifc_len/sizeof(entries[0]);i++) {
                const uint32_t ip=ntohl(((struct sockaddr_in *)
                    (void *)&entries[i].ifr_addr)->sin_addr.s_addr);
                if(!strcmp(entries[i].ifr_name,"eth0") &&
                   (ip>>8)==UINT32_C(0x0a0002))first=1;
                if(!strcmp(entries[i].ifr_name,"eth1") &&
                   (ip>>8)==UINT32_C(0x0a0003))second=1;
            }
            ready=first && second;
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

int main(void) {
    ncat_http();
    /* Preserves the original M174 devfs/20-ELF/gui PID1, so QMP still
     * exercises five physical screen states and shell-launched fifth LVGL. */
    return m174_original_main();
}
