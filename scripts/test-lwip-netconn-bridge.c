/* Actual official lwIP UDP/TCP loopback via a bounded native worker bridge.
 * The test caller is deliberately NOT a native kthread; it cannot invoke
 * a blocking netconn API and only submits/collects immutable commands. */
#include "../ports/lwip/netconn_bridge.h"
#include "../kernel/task/kthread.h"
#include "../kernel/errno.h"
#include "lwip/tcpip.h"
#include "lwip/sys.h"
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static uint64_t now_ns;
uint64_t nas_arch_monotonic_ns(void){return now_ns;}
void nas_print(const char *s){fputs(s,stdout);}
__attribute__((noreturn)) void nas_arch_halt(void){abort();}
void *nas_heap_calloc(size_t n,size_t size){return calloc(n,size);}
bool nas_heap_free(void *p){free(p);return true;}

static unsigned tcpip_ready;
static void ready(void *unused){(void)unused;tcpip_ready++;}
static struct nas_lwip_bridge_address lo(unsigned port){
 return (struct nas_lwip_bridge_address){.ipv4=UINT32_C(0x7f000001),
                                         .port=(uint16_t)port};
}
static struct nas_lwip_bridge_result finish(struct nas_lwip_bridge_id id){
 struct nas_lwip_bridge_result result={0};
 for(unsigned i=0;i<500;i++){
     int got=nas_lwip_bridge_take(id,&result);
     assert(got>=0);
     if(got==1)return result;
     assert(nas_kthread_run(1)==1);
 }
 fputs("native bridge completion never scheduled\n",stderr);
 abort();
}
static struct nas_lwip_bridge_result command(struct nas_lwip_bridge_id id,
                                             enum nas_lwip_bridge_op op,
                                             struct nas_lwip_bridge_address ep,
                                             unsigned accept_slot,
                                             const void *data,unsigned length){
 struct nas_lwip_bridge_request req={
     .op=op,.address=ep,.accept_slot=(uint16_t)accept_slot,
     .backlog=4,.bytes=data,.length=(uint16_t)length
 };
 assert(nas_lwip_bridge_submit(id,&req)==0);
 return finish(id);
}
static struct nas_lwip_bridge_id open_conn(unsigned slot,
                                           enum nas_lwip_bridge_type type){
 struct nas_lwip_bridge_id id={0};
 assert(nas_lwip_bridge_open(slot,type,&id)==0);
 assert(id.slot==slot && id.generation);
 assert(finish(id).status==0);
 return id;
}
int main(void){
 nas_kthread_system_init();
 sys_init();
 tcpip_init(ready,NULL);
 assert(nas_lwip_bridge_start());
 assert(!nas_lwip_bridge_start());
 assert(nas_kthread_run(1)==1 && tcpip_ready==1);

 /* Saturate the bounded command ring without dispatching the worker. A
  * notification must be consumed once per command, not accumulate as
  * stale semaphore tokens after a bulk drain. */
 struct nas_lwip_bridge_id burst[NAS_LWIP_BRIDGE_QUEUE];
 for(unsigned i=0;i<NAS_LWIP_BRIDGE_QUEUE;i++) {
     /* lwIP defaults to four UDP PCBs; mix TCP for eight Netconn objects. */
     enum nas_lwip_bridge_type type=i<4 ? NAS_LWIP_BRIDGE_UDP : NAS_LWIP_BRIDGE_TCP;
     assert(nas_lwip_bridge_open(50+i,type,&burst[i])==0);
 }
 struct nas_lwip_bridge_id overflow={0};
 assert(nas_lwip_bridge_open(58,NAS_LWIP_BRIDGE_UDP,&overflow)==-EAGAIN);
 for(unsigned i=0;i<NAS_LWIP_BRIDGE_QUEUE;i++) {
     struct nas_lwip_bridge_result opened=finish(burst[i]);
     if(opened.status)fprintf(stderr,"burst open %u status=%d\n",i,opened.status);
     assert(opened.status==0);
 }
 /* 8 commands consume exactly 8 semaphore tokens. The fourth/last
  * budget yields once; the next slice parks, the following has no work. */
 assert(nas_kthread_run(1)==1);
 assert(nas_kthread_run(1)==0);
 for(unsigned i=0;i<NAS_LWIP_BRIDGE_QUEUE;i++)
     assert(nas_lwip_bridge_release(burst[i])==0);
 unsigned drained=0;
 for(;drained<200;drained++)if(nas_kthread_run(1)==0)break;
 assert(drained<200);

 /* A final close before OPEN reaches lwIP must not leak a Netconn or allow
  * premature reuse of the same global OpenFile identity. */
 struct nas_lwip_bridge_id abandoned={0},reused={0};
 assert(nas_lwip_bridge_open(40,NAS_LWIP_BRIDGE_UDP,&abandoned)==0);
 assert(nas_lwip_bridge_release(abandoned)==0);
 assert(!nas_lwip_bridge_alive(abandoned));
 assert(nas_lwip_bridge_open(40,NAS_LWIP_BRIDGE_UDP,&reused)==-ENFILE);
 for(unsigned tries=0;tries<12;tries++) {
     int rc=nas_lwip_bridge_open(40,NAS_LWIP_BRIDGE_UDP,&reused);
     if(!rc)break;
     assert(rc==-ENFILE && nas_kthread_run(1)==1);
 }
 assert(reused.generation && reused.generation!=abandoned.generation);
 assert(finish(reused).status==0);
 assert(nas_lwip_bridge_release(reused)==0);

 struct nas_lwip_bridge_id udp_server=open_conn(20,NAS_LWIP_BRIDGE_UDP);
 struct nas_lwip_bridge_id udp_client=open_conn(21,NAS_LWIP_BRIDGE_UDP);
 assert(command(udp_server,NAS_LWIP_BRIDGE_BIND,lo(35007),0,NULL,0).status==0);
 char data[]="original udp bytes";
 struct nas_lwip_bridge_request send={.op=NAS_LWIP_BRIDGE_SEND,
     .address=lo(35007),.bytes=(const uint8_t *)data,
     .length=(uint16_t)(sizeof(data)-1)};
 assert(nas_lwip_bridge_submit(udp_client,&send)==0);
 memset(data,'X',sizeof(data)-1); /* caller storage cannot escape command */
 struct nas_lwip_bridge_result result=finish(udp_client);
 assert(result.status==0 && result.length==sizeof(data)-1);
 result=command(udp_server,NAS_LWIP_BRIDGE_RECV,lo(0),0,NULL,NAS_LWIP_BRIDGE_DATA);
 assert(result.status==0 && result.length==sizeof(data)-1);
 assert(!memcmp(result.bytes,"original udp bytes",result.length));
 assert(!result.truncated && result.peer.ipv4==UINT32_C(0x7f000001));
 const char short_udp[]="second datagram";
 assert(command(udp_client,NAS_LWIP_BRIDGE_SEND,lo(35007),0,
                short_udp,sizeof(short_udp)-1).status==0);
 result=command(udp_server,NAS_LWIP_BRIDGE_RECV,lo(0),0,NULL,6);
 assert(result.status==0 && result.length==6 && result.truncated);
 assert(!memcmp(result.bytes,"second",6));

 struct nas_lwip_bridge_id listener=open_conn(30,NAS_LWIP_BRIDGE_TCP);
 assert(command(listener,NAS_LWIP_BRIDGE_BIND,lo(36007),0,NULL,0).status==0);
 assert(command(listener,NAS_LWIP_BRIDGE_LISTEN,lo(0),0,NULL,0).status==0);
 struct nas_lwip_bridge_id client=open_conn(31,NAS_LWIP_BRIDGE_TCP);
 result=command(client,NAS_LWIP_BRIDGE_CONNECT,lo(36007),0,NULL,0);
 assert(result.status==0 || result.status==-EINPROGRESS);
 struct nas_lwip_bridge_id accepted={0};
 for(unsigned tries=0;tries<120;tries++){
     result=command(listener,NAS_LWIP_BRIDGE_ACCEPT,lo(0),32,NULL,0);
     if(result.status==0){accepted=result.accepted;break;}
     assert(result.status==-EAGAIN);
     assert(nas_kthread_run(1)==1);
 }
 assert(accepted.slot==32 && accepted.generation);
 const char tcp_payload[]="official lwIP TCP";
 for(unsigned tries=0;tries<120;tries++){
     result=command(client,NAS_LWIP_BRIDGE_SEND,lo(0),0,
                    tcp_payload,sizeof(tcp_payload)-1);
     if(result.status==0){assert(result.length==sizeof(tcp_payload)-1);break;}
     assert(result.status==-EAGAIN || result.status==-EINPROGRESS);
     assert(nas_kthread_run(1)==1);
 }
 assert(result.status==0);
 for(unsigned tries=0;tries<120;tries++){
     result=command(accepted,NAS_LWIP_BRIDGE_RECV,lo(0),0,NULL,5);
     if(result.status==0 && result.length)break;
     assert(result.status==-EAGAIN);
     assert(nas_kthread_run(1)==1);
 }
 assert(result.status==0 && result.length==5);
 assert(!memcmp(result.bytes,tcp_payload,5));
 result=command(accepted,NAS_LWIP_BRIDGE_RECV,lo(0),0,NULL,NAS_LWIP_BRIDGE_DATA);
 assert(result.status==0 && result.length==sizeof(tcp_payload)-1-5);
 assert(!memcmp(result.bytes,tcp_payload+5,result.length));

 /* An ACCEPT child exists before userspace takes the listener's result.
  * Final-close of the listener owns and retires that unclaimed Netconn,
  * posting its own wake token even if the command queue is now empty. */
 struct nas_lwip_bridge_id extra=open_conn(33,NAS_LWIP_BRIDGE_TCP);
 result=command(extra,NAS_LWIP_BRIDGE_CONNECT,lo(36007),0,NULL,0);
 assert(result.status==0 || result.status==-EINPROGRESS);
 const struct nas_lwip_bridge_id orphan={34,1}; /* first incarnation */
 bool unclaimed=false;
 for(unsigned tries=0;tries<120 && !unclaimed;tries++) {
     struct nas_lwip_bridge_request req={
         .op=NAS_LWIP_BRIDGE_ACCEPT,.accept_slot=34
     };
     assert(nas_lwip_bridge_submit(listener,&req)==0);
     for(unsigned slice=0;slice<15;slice++) {
         if(nas_lwip_bridge_alive(orphan)){unclaimed=true;break;}
         if(nas_kthread_run(1)==0)break;
     }
     if(!unclaimed) {
         result=finish(listener);
         assert(result.status==-EAGAIN);
     }
 }
 assert(unclaimed);
 assert(nas_lwip_bridge_release(listener)==0); /* do not take ACCEPT result */
 assert(nas_lwip_bridge_release(extra)==0);
 for(unsigned tries=0;tries<150 && nas_lwip_bridge_alive(orphan);tries++)
     assert(nas_kthread_run(1)==1);
 assert(!nas_lwip_bridge_alive(orphan));
 struct nas_lwip_bridge_id reclaimed={0};
 for(unsigned tries=0;tries<150;tries++) {
     int rc=nas_lwip_bridge_open(34,NAS_LWIP_BRIDGE_TCP,&reclaimed);
     if(!rc)break;
     assert(rc==-ENFILE && nas_kthread_run(1)==1);
 }
 assert(reclaimed.generation>orphan.generation);
 assert(finish(reclaimed).status==0);
 assert(nas_lwip_bridge_release(reclaimed)==0);

 /* Final File close never invokes netconn_delete on this test (user) stack. */
 assert(nas_lwip_bridge_release(accepted)==0);
 assert(nas_lwip_bridge_release(client)==0);
 assert(nas_lwip_bridge_release(udp_server)==0);
 assert(nas_lwip_bridge_release(udp_client)==0);
 assert(nas_lwip_bridge_take(udp_server,&result)==-EBADF);
 assert(nas_lwip_bridge_submit(udp_client,&(struct nas_lwip_bridge_request){
         .op=NAS_LWIP_BRIDGE_RECV})==-EBADF);
 for(unsigned tries=0;tries<300;tries++){
     struct nas_lwip_bridge_id replacement={0};
     int rc=nas_lwip_bridge_open(20,NAS_LWIP_BRIDGE_UDP,&replacement);
     if(!rc){
         assert(replacement.generation!=udp_server.generation);
         assert(finish(replacement).status==0);
         assert(nas_lwip_bridge_release(replacement)==0);
         puts("[LWIP NETCONN BRIDGE PASS] real upstream UDP/TCP, copied commands, async native Netconn, FD-slot generation, final release");
         return 0;
     }
     assert(rc==-ENFILE);
     assert(nas_kthread_run(1)==1);
 }
 abort();
}
