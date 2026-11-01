/* Opt-in native network bootstrap. Official lwIP owns all wire protocols;
 * one NaSeed kernel worker only schedules bounded NIC completion polling. */
#include "netif_port.h"
#include "network_boot.h"
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
 uint32_t dhcp_ipv4_host_order; /* copied only by upstream core callback */
 uint8_t dhcp_prefix_len; /* derived from the same real DHCP netif lease */
 uint16_t dhcp_mtu; /* native core netif MTU at the actual DHCP lease */
} network;

/* The bootstrap attaches at most one physical netif; give Linux's name to
 * the upstream netconn_bind_if() interface index, without guessing ns0/eth0
 * index numbering or routing through a different interface. */
int nas_lwip_guest_ifindex(const char *device_name){
 if(!device_name || !network.ethernet.attached ||
    !network.ethernet.device ||
    strcmp(device_name,network.device_name))return -ENODEV;
 unsigned index=netif_get_index(&network.ethernet.interface);
 return index ? (int)index:-ENODEV;
}
int nas_lwip_guest_ifname(unsigned index,
                         char output[NAS_LWIP_DEVICE_NAME_MAX]){
 if(!output || !index || !network.ethernet.attached ||
    !network.ethernet.device ||
    index!=netif_get_index(&network.ethernet.interface))return -ENODEV;
 memcpy(output,network.device_name,sizeof(network.device_name));
 return 0;
}
unsigned nas_lwip_guest_ipv4_ifconf(
    struct nas_lwip_guest_ipv4_if out[2]) {
 if(!out)return 0;
 out[0]=(struct nas_lwip_guest_ipv4_if){
     .name="lo",.ipv4_host_order=UINT32_C(0x7f000001),.prefix_len=8,
     .mtu=65536U
 };
 unsigned count=1;
 const uint32_t address=network.dhcp_ipv4_host_order;
 if(network.ethernet.attached && network.ethernet.device && address &&
    network.dhcp_prefix_len) {
     out[count]=(struct nas_lwip_guest_ipv4_if){.ipv4_host_order=address,
               .prefix_len=network.dhcp_prefix_len,.mtu=network.dhcp_mtu};
     memcpy(out[count].name,network.device_name,sizeof(network.device_name));
     ++count;
 }
 return count;
}

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
 if(!netif)return;
 const ip4_addr_t *addr=netif_ip4_addr(netif);
 /* Never manufacture a guest interface address from a DHCP request or
  * a merely attached NIC; report only the real core-owned IPv4 lease. */
 network.dhcp_ipv4_host_order=netif_is_up(netif) &&
     netif_is_link_up(netif) && dhcp_supplied_address(netif) &&
     !ip4_addr_isany_val(*addr) ?
     lwip_ntohl(ip4_addr_get_u32(addr)) : 0;
 /* Snapshot the ACTUAL DHCP subnet mask with the address, not a hardwired
  * 10.0.2.0/24 assumption. Linux RTM_NEWADDR stores a CIDR prefix length. */
 network.dhcp_prefix_len=0;
 network.dhcp_mtu=network.dhcp_ipv4_host_order ? netif->mtu : 0;
 if(network.dhcp_ipv4_host_order) {
     uint32_t mask=lwip_ntohl(ip4_addr_get_u32(netif_ip4_netmask(netif)));
     uint32_t rest=mask;
     uint8_t length=0;
     while(rest&UINT32_C(0x80000000)) {length++;rest<<=1;}
     if(!rest)network.dhcp_prefix_len=length;
 }
 nas_print("[LWIP NETIF STATUS] up=");nas_dec(netif_is_up(netif));
 nas_print(" link=");nas_dec(netif_is_link_up(netif));
 nas_print(" dhcp=");nas_dec(dhcp_supplied_address(netif));
 nas_print(" ip=");nas_dec(ip4_addr1(addr));nas_print(".");
 nas_dec(ip4_addr2(addr));nas_print(".");
 nas_dec(ip4_addr3(addr));nas_print(".");
 nas_dec(ip4_addr4(addr));nas_print("\n");
 if(!netif_is_up(netif) || !netif_is_link_up(netif) ||
    !dhcp_supplied_address(netif))return;
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
         /* Covers a DHCP reply accepted while the callback was being
          * registered; later address changes still use lwIP's callback. */
         network_status(&network.ethernet.interface);
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
