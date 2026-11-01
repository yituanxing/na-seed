/* Official lwIP 2.2.1 + real NaSeed kernel continuation and IPC test.
 * Hosted callbacks are not proof of live QEMU NIC/AF_INET. */
#include "../kernel/task/kthread.h"
#include "../kernel/task/k_sync.h"
#include "../kernel/fd.h"
#include "../ports/lwip/netif_port.h"
#include "../kernel/errno.h"
#include "lwip/sys.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

static uint64_t now_ns;
uint64_t nas_arch_monotonic_ns(void){return now_ns;}
void nas_print(const char *s){fputs(s,stdout);}
__attribute__((noreturn)) void nas_arch_halt(void){abort();}
void *nas_heap_calloc(size_t n,size_t size){return calloc(n,size);}
bool nas_heap_free(void *p){free(p);return true;}
/* Netconn ACCEPT validates the same OpenFile lease as the real kernel;
 * this fixture never opens a user socket, but must provide the registry. */
struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];

static sys_sem_t sem;
static sys_mbox_t box;
static unsigned phase;
static void sem_waiter(void *ignored){
 (void)ignored;
 assert(sys_arch_sem_wait(&sem,100)==11);
 phase=1;
}
static void sem_sender(void *ignored){
 (void)ignored;
 now_ns=UINT64_C(11000000);
 sys_sem_signal(&sem);
}
static void timed_waiter(void *ignored){
 (void)ignored;
 assert(sys_arch_sem_wait(&sem,5)==SYS_ARCH_TIMEOUT);
 phase=2;
}
static void box_waiter(void *ignored){
 (void)ignored;
 void *value=NULL;
 assert(sys_arch_mbox_fetch(&box,&value,20)==3);
 assert(value==(void *)(uintptr_t)0xbeef);
 phase=3;
}
static void box_sender(void *ignored){
 (void)ignored;
 now_ns+=UINT64_C(3000000);
 sys_mbox_post(&box,(void *)(uintptr_t)0xbeef);
}
static void sleep_worker(void *ignored){
 (void)ignored;
 sys_msleep(7);
 phase=4;
}
static unsigned initialized,callbacks;
static unsigned udp_received;
static struct udp_pcb *udp_server,*udp_client;
static void recv_udp(void *arg,struct udp_pcb *pcb,struct pbuf *p,
                     const ip_addr_t *remote,u16_t port){
 (void)arg;(void)pcb;(void)remote;(void)port;
 assert(p && p->tot_len==5);
 uint8_t payload[5];
 assert(pbuf_copy_partial(p,payload,5,0)==5);
 assert(payload[0]=='h' && payload[4]=='o');
 udp_received++;
 pbuf_free(p);
}
static void send_loopback_udp(void *unused){
 (void)unused;
 udp_server=udp_new();udp_client=udp_new();
 assert(udp_server && udp_client);
 ip_addr_t loop;
 IP_ADDR4(&loop,127,0,0,1);
 assert(udp_bind(udp_server,&loop,34711)==ERR_OK);
 udp_recv(udp_server,recv_udp,NULL);
 struct pbuf *p=pbuf_alloc(PBUF_TRANSPORT,5,PBUF_RAM);
 assert(p && pbuf_take(p,"hello",5)==ERR_OK);
 assert(udp_sendto(udp_client,p,&loop,34711)==ERR_OK);
 pbuf_free(p);
}
static struct nas_lwip_eth eth_port;
static unsigned outbound,received;
static uint8_t last_tx[1518];
static unsigned last_tx_size;
static int fake_tx(void *ctx,const uint8_t *frame,uint32_t bytes){
 (void)ctx;
 assert(bytes>=14 && bytes<=sizeof(last_tx));
 for(unsigned i=0;i<bytes;i++)last_tx[i]=frame[i];
 last_tx_size=bytes;outbound++;
 return 0;
}
static int fake_poll(void *ctx,uint32_t budget,nas_netdev_rx receive,
                     void *consumer){
 (void)ctx;
 if(!budget || received)return 0;
 uint8_t frame[60]={0xff,0xff,0xff,0xff,0xff,0xff,
                    0x52,0x54,0,0x12,0x34,0x56,0x08,0x06};
 int rc=receive(consumer,frame,sizeof(frame));
 if(rc)return rc;
 received++;
 return 1;
}
static bool fake_link(void *ctx){(void)ctx;return true;}
static const struct nas_netdev_ops fake_ops={
 .transmit=fake_tx,.poll=fake_poll,.link_up=fake_link
};
static struct nas_netdev netdev={.name="eth-test",
  .mac={0x52,0x54,0,0x12,0x34,0x56},.mtu=1500,
  .backend=&eth_port,.ops=&fake_ops};
static int attach_rc,detach_rc;
static void attach_netif(void *unused){
 (void)unused;
 attach_rc=nas_lwip_eth_core_attach(&eth_port,"eth-test",true);
}
static void detach_netif(void *unused){
 (void)unused;
 detach_rc=nas_lwip_eth_core_detach(&eth_port);
}
static void tcpip_ready(void *arg){
 assert(arg==&initialized);
 initialized++;
}
static void tcpip_callback_check(void *arg){
 assert(arg==&callbacks);
 callbacks++;
}

int main(void){
 nas_kthread_system_init();
 sys_init();
 assert(sys_now()==0 && sys_jiffies()==0);
 assert(sys_sem_new(&sem,1)==ERR_OK);
 assert(sys_sem_valid(&sem));
 assert(sys_arch_sem_wait(&sem,100)==0);
 struct nas_kthread_handle first={0},second={0};
 assert(nas_kthread_spawn(sem_waiter,NULL,&first));
 assert(nas_kthread_spawn(sem_sender,NULL,&second));
 assert(nas_kthread_run(1)==1 && phase==0);
 assert(nas_kthread_run(2)==2 && phase==1);
 assert(nas_kthread_spawn(timed_waiter,NULL,&first));
 assert(nas_kthread_run(1)==1);
 now_ns+=UINT64_C(6000000);
 assert(nas_kthread_timers_tick(now_ns)==1);
 assert(nas_kthread_run(1)==1 && phase==2);
 sys_sem_free(&sem);
 assert(!sys_sem_valid(&sem));

 assert(sys_mbox_new(&box,NAS_KMAILBOX_CAP+1)==ERR_MEM);
 assert(!sys_mbox_valid(&box));
 assert(sys_mbox_new(&box,NAS_KMAILBOX_CAP)==ERR_OK);
 for(unsigned i=0;i<NAS_KMAILBOX_CAP;i++)
     assert(sys_mbox_trypost(&box,(void *)(uintptr_t)(i+1))==ERR_OK);
 assert(sys_mbox_trypost_fromisr(&box,NULL)==ERR_MEM);
 for(unsigned i=0;i<NAS_KMAILBOX_CAP;i++){
     void *value=NULL;
     assert(sys_arch_mbox_tryfetch(&box,&value)==0);
     assert(value==(void *)(uintptr_t)(i+1));
 }
 assert(sys_arch_mbox_tryfetch(&box,NULL)==SYS_MBOX_EMPTY);
 assert(nas_kthread_spawn(box_waiter,NULL,&first));
 assert(nas_kthread_run(1)==1);
 assert(nas_kthread_spawn(box_sender,NULL,&second));
 assert(nas_kthread_run(2)==2 && phase==3);
 sys_mbox_post(&box,NULL);
 assert(sys_arch_mbox_tryfetch(&box,NULL)==0);
 sys_mbox_free(&box);
 assert(!sys_mbox_valid(&box));
 assert(nas_kthread_spawn(sleep_worker,NULL,&first));
 assert(nas_kthread_run(1)==1);
 now_ns+=UINT64_C(7000000);
 assert(nas_kthread_timers_tick(now_ns)==1);
 assert(nas_kthread_run(1)==1 && phase==4);

 /* This starts the official lwIP TCP/IP core on a genuinely suspended
  * NaSeed C stack, then wakes it through its native bounded mailbox. */
 tcpip_init(tcpip_ready,&initialized);
 assert(nas_kthread_run(1)==1 && initialized==1);
 assert(netif_list && netif_list->name[0]=='l' &&
        netif_list->name[1]=='o'); /* lwIP-owned 127.0.0.1 */
 assert(tcpip_callback(tcpip_callback_check,&callbacks)==ERR_OK);
 assert(nas_kthread_run(1)==1 && callbacks==1);
 assert(tcpip_callback(send_loopback_udp,NULL)==ERR_OK);
 for(unsigned i=0;i<4 && !udp_received;i++)assert(nas_kthread_run(1)==1);
 assert(udp_received==1); /* actual official UDP over lwIP lo, no pipe */
 assert(nas_netdev_register(&netdev)==0);
 assert(tcpip_callback(attach_netif,NULL)==ERR_OK);
 assert(nas_kthread_run(1)==1 && attach_rc==0);
 assert(eth_port.attached && eth_port.device==&netdev);
 assert(eth_port.interface.mtu==1500 && outbound>=1);
 assert(last_tx_size>=14 && last_tx[12]==8 && last_tx[13]==0);
 assert(nas_netdev_unregister(&netdev)==-EBUSY);
 assert(nas_lwip_eth_poll(&eth_port,4)==1 && received==1);
 assert(nas_kthread_run(1)==1); /* official Ethernet/ARP input executed */
 assert(tcpip_callback(detach_netif,NULL)==ERR_OK);
 assert(nas_kthread_run(1)==1 && detach_rc==0);
 assert(nas_netdev_unregister(&netdev)==0);
 assert(nas_kthread_live()==1); /* tcpip_thread parked, not a fake inline call */
 puts("[LWIP NATIVE NETIF PASS] upstream tcpip_thread, native waits/timeouts, real UDP 127.0.0.1, DHCP TX, Ethernet RX, pins, teardown");
}
