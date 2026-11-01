/* Opt-in native network bootstrap. Official lwIP owns all wire protocols;
 * one NaSeed kernel worker only schedules bounded NIC completion polling. */
#include "netif_port.h"
#include "netconn_bridge.h"
#include "../../kernel/arch/arch.h"
#include "../../kernel/task/k_sync.h"
#include "../../kernel/task/kthread.h"
#include "../../kernel/drivers/serial.h"
#include "../../kernel/errno.h"
#include "../../kernel/lib/string.h"
#include "../../kernel/time/clock.h"
#include "lwip/tcpip.h"
#include "lwip/sys.h"
#include "lwip/dhcp.h"
#include "lwip/ip4_addr.h"
#include <stdbool.h>
#include <stdint.h>

#if defined(NAS_ENABLE_VIRTIO_BLK) || defined(NAS_ENABLE_VIRTIO_NET)
extern int nas_platform_virtio_blk_probe(void);
#endif

#define NAS_NET_RX_BUDGET 8U
#define NAS_NET_FALLBACK_POLL_NS UINT64_C(10000000)
static struct {
 struct nas_lwip_eth ethernet;
 char device_name[16]; /* Linux IFNAMSIZ: selection is not driver-specific. */
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
/* Executed by lwIP on its core thread, after DHCP has actually assigned an
 * address. Merely starting DHCP is not evidence of a working NIC/lease. */
static void network_status(struct netif *netif){
 if(!netif || !netif_is_up(netif) || !netif_is_link_up(netif) ||
    !dhcp_supplied_address(netif))return;
 const ip4_addr_t *addr=netif_ip4_addr(netif);
 if(ip4_addr_isany_val(*addr))return;
 nas_print("[LWIP DHCP BOUND] addr=");
 nas_dec(ip4_addr1(addr));nas_print(".");
 nas_dec(ip4_addr2(addr));nas_print(".");
 nas_dec(ip4_addr3(addr));nas_print(".");
 nas_dec(ip4_addr4(addr));nas_print("\n");
}
static void receive_worker(void *ctx){
 (void)ctx;
 if(!nas_ksem_wait(&network.rx_wakeup))return;
 while(!network.stop && network.ethernet.attached){
     int received=nas_lwip_eth_poll(&network.ethernet,NAS_NET_RX_BUDGET);
     /* TX and RX share the U-Boot queue owner lock. Contention or pbuf
      * pressure is not an unplugged/poisoned NIC: retry after the normal
      * IRQ-or-deadline wait rather than permanently detaching the netif. */
     if(received==-EBUSY || received==-EAGAIN ||
        received==-ENOBUFS || received==-ENOMEM)received=0;
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
     if(nas_lwip_eth_core_attach(&network.ethernet,network.device_name,true)){
         nas_print("[LWIP] cannot attach selected NIC; loopback remains active\n");
         network.stop=true;
     }else if(nas_netdev_set_rx_wake(network.ethernet.device,
                                     wake_rx,&network.rx_wakeup)){
         network.stop=true;
         (void)nas_lwip_eth_core_detach(&network.ethernet);
         nas_print("[LWIP] cannot install NIC RX notification\n");
     }else{
         netif_set_status_callback(&network.ethernet.interface,network_status);
         nas_print("[LWIP] native ");nas_print(network.device_name);
         nas_print(" attached; DHCP started\n");
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
 /* Device discovery is stack-neutral. Another NIC driver may publish a
  * different interface name without changing the lwIP bootstrap. This
  * initial port still manages only one netif; multi-NIC is a later gate. */
 struct nas_netdev *eth=nas_netdev_get_ordinal(0);
 if(eth){
     size_t len=strnlen(eth->name,sizeof(network.device_name));
     if(len && len<sizeof(network.device_name)){
         memcpy(network.device_name,eth->name,len+1U);
         network.ethernet_available=true;
     }
     (void)nas_netdev_put(eth);
 }
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
