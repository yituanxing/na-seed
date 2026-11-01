/* Opt-in native network bootstrap. Official lwIP owns all wire protocols;
 * one NaSeed kernel worker only schedules bounded NIC completion polling. */
#include "netif_port.h"
#include "netconn_bridge.h"
#include "../../kernel/arch/arch.h"
#include "../../kernel/task/k_sync.h"
#include "../../kernel/task/kthread.h"
#include "../../kernel/drivers/serial.h"
#include "../../kernel/time/clock.h"
#include "lwip/tcpip.h"
#include "lwip/sys.h"
#include <stdbool.h>
#include <stdint.h>

#if defined(NAS_ENABLE_VIRTIO_BLK) || defined(NAS_ENABLE_VIRTIO_NET)
extern int nas_platform_virtio_blk_probe(void);
#endif

#define NAS_NET_RX_BUDGET 8U
#define NAS_NET_FALLBACK_POLL_NS UINT64_C(10000000)
static struct {
 struct nas_lwip_eth ethernet;
 struct nas_ksem rx_wakeup;
 struct nas_kthread_handle rx_handle;
 bool ethernet_available,ready,stop;
} network;

/* Called by nas_netdev_notify_rx from the transport IRQ/scheduled signal.
 * It never executes lwIP or polls the queue inside an IRQ. */
static void wake_rx(void *ctx){
 struct nas_ksem *sem=ctx;
 (void)nas_ksem_post(sem);
}
static void detach_failed_netif(void *ctx){
 (void)ctx;
 if(!network.ethernet.attached)return;
 (void)nas_netdev_set_rx_wake(network.ethernet.device,NULL,NULL);
 (void)nas_lwip_eth_core_detach(&network.ethernet);
}
static void receive_worker(void *ctx){
 (void)ctx;
 if(!nas_ksem_wait(&network.rx_wakeup))return;
 while(!network.stop && network.ethernet.attached){
     int received=nas_lwip_eth_poll(&network.ethernet,NAS_NET_RX_BUDGET);
     if(received<0){
         nas_print("[LWIP] NIC RX failed; detaching netif\n");
         network.stop=true;
         (void)tcpip_callback(detach_failed_netif,NULL);
         return;
     }
     if(received==(int)NAS_NET_RX_BUDGET){
         /* Yield after each full budget; never starve user tasks or the
          * actual lwIP tcpip_thread with an always-ready NIC worker. */
         (void)nas_kthread_yield();
         continue;
     }
     const uint64_t deadline=nas_time_add_deadline(
         nas_arch_monotonic_ns(),NAS_NET_FALLBACK_POLL_NS);
     enum nas_kwait_result result=nas_ksem_wait_until(&network.rx_wakeup,
                                                       deadline);
     if(result==NAS_KWAIT_INVALID){
         network.stop=true;
         (void)tcpip_callback(detach_failed_netif,NULL);
         return;
     }
 }
}
/* This callback runs ON the real lwIP tcpip_thread C stack. Starting DHCP,
 * adding the netif and registering the event callback all belong here. */
static void tcpip_ready(void *unused){
 (void)unused;
 if(network.ethernet_available){
     if(nas_lwip_eth_core_attach(&network.ethernet,"eth0",true)){
         nas_print("[LWIP] cannot attach eth0; loopback remains active\n");
         network.stop=true;
     }else if(nas_netdev_set_rx_wake(network.ethernet.device,
                                     wake_rx,&network.rx_wakeup)){
         network.stop=true;
         (void)nas_lwip_eth_core_detach(&network.ethernet);
         nas_print("[LWIP] cannot install NIC RX notification\n");
     }else{
         nas_print("[LWIP] original U-Boot eth0 attached; DHCP started\n");
     }
 }
 network.ready=true;
 (void)nas_ksem_post(&network.rx_wakeup);
}
bool nas_lwip_guest_boot(void){
 if(network.ready || network.ethernet_available)return false;
#if defined(NAS_ENABLE_VIRTIO_BLK) || defined(NAS_ENABLE_VIRTIO_NET)
 /* The same platform bus enumerates block and net. It is idempotent if
  * lwext4 already scanned before us; no filesystem owns this bootstrap. */
 if(nas_platform_virtio_blk_probe()<0)return false;
#endif
 struct nas_netdev *eth=nas_netdev_get("eth0");
 if(eth){network.ethernet_available=true;(void)nas_netdev_put(eth);}
 nas_ksem_init(&network.rx_wakeup,0);
 sys_init();
 tcpip_init(tcpip_ready,NULL);
 if(network.ethernet_available &&
    !nas_kthread_spawn(receive_worker,NULL,&network.rx_handle))
     return false;
 /* Netconn calls must run on a native kernel C stack; user Task syscalls
  * are only producers of bounded commands and never park in lwIP sys_arch. */
 if(!nas_lwip_bridge_start())return false;
 nas_print("[LWIP] official TCP/IP thread scheduled; native loopback enabled\n");
 return true;
}
