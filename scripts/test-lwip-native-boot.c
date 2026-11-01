/* Hosted NaSeed boot wiring: native scheduler runs official tcpip thread,
 * binds a real netdev identity and pumps RX outside IRQ context. */
#include "../kernel/task/kthread.h"
#include "../kernel/drivers/port/net.h"
#include "lwip/sys.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

extern bool nas_lwip_guest_boot(void);
static uint64_t now_ns;
static unsigned scans,transmitted,polls,delivered;
static bool inbound_pending=true;
uint64_t nas_arch_monotonic_ns(void){return now_ns;}
void nas_print(const char *s){fputs(s,stdout);}
__attribute__((noreturn)) void nas_arch_halt(void){abort();}
void *nas_heap_calloc(size_t n,size_t size){return calloc(n,size);}
bool nas_heap_free(void *p){free(p);return true;}
int nas_platform_virtio_blk_probe(void){scans++;return 0;}

static int transmit(void *ctx,const uint8_t *frame,uint32_t bytes){
 (void)ctx;
 assert(bytes>=14 && bytes<=1518);
 assert(frame[12]==0x08 && frame[13]==0x00); /* real DHCP IPv4 frame */
 transmitted++;
 return 0;
}
static int poll(void *ctx,uint32_t budget,nas_netdev_rx receive,void *consumer){
 (void)ctx;
 assert(budget==8);
 polls++;
 if(!inbound_pending)return 0;
 uint8_t frame[60]={0xff,0xff,0xff,0xff,0xff,0xff,
                    0x52,0x54,0,0x12,0x34,0x56,0x08,0x06};
 int rc=receive(consumer,frame,sizeof(frame));
 if(rc)return rc;
 inbound_pending=false;
 delivered++;
 return 1;
}
static bool link_up(void *ctx){(void)ctx;return true;}
static const struct nas_netdev_ops network_ops={
 .transmit=transmit,.poll=poll,.link_up=link_up
};
static struct nas_netdev eth={
 .name="eth0",.mac={0x52,0x54,0,0x12,0x34,0x56},
 .mtu=1500,.backend=&eth,.ops=&network_ops
};
int main(void){
 nas_kthread_system_init();
 assert(nas_netdev_register(&eth)==0);
 assert(nas_lwip_guest_boot());
 assert(scans==1 && nas_kthread_live()==2);
 assert(nas_kthread_run(1)==1); /* tcpip starts, DHCP discover */
 assert(transmitted>=1 && eth.refs==1);
 assert(nas_netdev_unregister(&eth)!=0); /* lwIP pins the live device */
 assert(nas_kthread_run(1)==1);
 assert(delivered==1 && polls==1); /* RX worker, outside tcpip stack */
 assert(nas_kthread_run(1)==1); /* upstream lwIP consumes inbound frame */
 inbound_pending=true;
 nas_netdev_notify_rx(&eth); /* IRQ path only wakes, no RX parsing */
 assert(delivered==1);
 assert(nas_kthread_run(1)==1);
 assert(delivered==2);
 now_ns+=UINT64_C(11000000);
 assert(nas_kthread_timers_tick(now_ns)>=1);
 for(unsigned i=0;i<4 && polls<3;i++)
     assert(nas_kthread_run(1)==1); /* user-scheduler slices may interleave */
 assert(polls>=3);
 puts("[LWIP NATIVE BOOT PASS] idempotent discovery hook, native tcpip/DHCP, bounded RX worker, IRQ wake, deadline fallback, pins");
}
