/* Native network bootstrap. Official lwIP owns all wire protocols;
 * one NaSeed kernel worker only schedules bounded NIC completion polling. */
#include "netif_port.h"
#include "network_boot.h"
#include "netconn_bridge.h"
#ifdef NAS_ENABLE_VIRTIO_NET
#include "../uboot/virtio_net_bus.h"
#endif
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

#if defined(NAS_ENABLE_VIRTIO_BLK) || defined(NAS_ENABLE_VIRTIO_NET) || defined(NAS_ENABLE_VIRTIO_INPUT) || defined(NAS_ENABLE_VIRTIO_GPU)
extern int nas_platform_virtio_blk_probe(void);
#endif

#define NAS_NET_RX_BUDGET 8U
#define NAS_NET_FALLBACK_POLL_NS UINT64_C(10000000)
/* Each pinned physical netif has an independent DHCP lease and link state.
 * This is a stack-side projection of the ONLY NetDevice registry; there is
 * no second device ownership table. The tcpip_thread owns netif mutation. */
struct nas_guest_nic {
 struct nas_lwip_eth ethernet;
 char name[NAS_LWIP_DEVICE_NAME_MAX];
 bool pending_attach,pending_detach,link_sync_queued;
 uint64_t rx_last_irq_sequence;
 uint32_t dhcp_ipv4_host_order;
 uint8_t dhcp_prefix_len;
};
static struct {
 struct nas_guest_nic nic[NAS_NETDEV_LIMIT];
 struct nas_ksem rx_wakeup;
 struct nas_kthread_handle rx_handle;
 bool starting,ready,stop;
 uint64_t rx_deferred_drains,rx_fallback_polls;
 bool irq_reported;
} network;

/* The receive worker, not the hard IRQ, owns packet processing and reports
 * the proof after two independently attached, IRQ-routed NICs have delivered
 * real packets. This is intentionally a diagnostic of actual driver state,
 * never a synthetic event inserted into an acceptance script. */
static void report_real_net_irq(bool all_irq_driven){
#ifdef NAS_ENABLE_VIRTIO_NET
 if(network.irq_reported || !all_irq_driven ||
    network.rx_fallback_polls)return;
 struct nas_virtio_net_irq_stats stats={0};
 nas_virtio_net_irq_totals(&stats);
 if(stats.irq_driven_nics!=2 || stats.irq_proven_nics!=2 ||
    stats.tx_proven_nics!=2 || !stats.tx_irq_wakes ||
    !stats.tx_worker_waits || stats.tx_legacy_polls ||
    !stats.irq_acks ||
    !stats.irq_rx_wakes || !stats.rx_completed ||
    !network.rx_deferred_drains)return;
 nas_print("[M176 NET RX IRQ STATS] nic_irq=");nas_dec(stats.irq_driven_nics);
 nas_print(" nic_proven=");nas_dec(stats.irq_proven_nics);
 nas_print(" irq_ack=");nas_dec(stats.irq_acks);
 nas_print(" irq_wakes=");nas_dec(stats.irq_rx_wakes);
 nas_print(" rx_packets=");nas_dec(stats.rx_completed);
 nas_print(" deferred_drains=");nas_dec(network.rx_deferred_drains);
 nas_print(" fallback_poll=");nas_dec(network.rx_fallback_polls);
 nas_print(" tx_nic_proven=");nas_dec(stats.tx_proven_nics);
 nas_print(" tx_irq_wakes=");nas_dec(stats.tx_irq_wakes);
 nas_print(" tx_worker_waits=");nas_dec(stats.tx_worker_waits);
 nas_print(" tx_legacy_polls=");nas_dec(stats.tx_legacy_polls);
 nas_print("\n[M176 NET RX IRQ ONLY PASS]\n");
 network.irq_reported=true;
#else
 (void)all_irq_driven;
#endif
}

unsigned nas_lwip_guest_if_snapshot(
    struct nas_lwip_guest_if out[NAS_LWIP_GUEST_IF_LIMIT]) {
 if(!out)return 0;
 out[0]=(struct nas_lwip_guest_if){
     .name="lo",.ifindex=1,.ipv4_host_order=UINT32_C(0x7f000001),
     .prefix_len=8,.mtu=65536U,.up=true,.link_up=true,.loopback=true
 };
 unsigned count=1;
 for(unsigned i=0;i<NAS_NETDEV_LIMIT;i++) {
     const struct nas_guest_nic *nic=&network.nic[i];
     if(!nic->ethernet.attached || !nic->ethernet.device)continue;
     const struct netif *netif=&nic->ethernet.interface;
     struct nas_lwip_guest_if *physical=&out[count++];
     *physical=(struct nas_lwip_guest_if){
         .ifindex=netif_get_index((struct netif *)netif),.mtu=netif->mtu,
         .up=netif_is_up(netif),.link_up=netif_is_link_up(netif)
     };
     memcpy(physical->name,nic->name,sizeof(physical->name));
     /* Only an actually assigned, live core-owned lease is visible. */
     if(physical->up && physical->link_up) {
         physical->ipv4_host_order=nic->dhcp_ipv4_host_order;
         physical->prefix_len=nic->dhcp_prefix_len;
     }
 }
 return count;
}
int nas_lwip_guest_ifindex(const char *device_name){
 if(!device_name)return -ENODEV;
 struct nas_lwip_guest_if interfaces[NAS_LWIP_GUEST_IF_LIMIT]={0};
 unsigned count=nas_lwip_guest_if_snapshot(interfaces);
 for(unsigned i=1;i<count;i++)
     if(!strcmp(device_name,interfaces[i].name))
         return interfaces[i].ifindex ? (int)interfaces[i].ifindex:-ENODEV;
 return -ENODEV;
}
int nas_lwip_guest_ifname(unsigned index,
                         char output[NAS_LWIP_DEVICE_NAME_MAX]){
 if(!output || !index)return -ENODEV;
 struct nas_lwip_guest_if interfaces[NAS_LWIP_GUEST_IF_LIMIT]={0};
 unsigned count=nas_lwip_guest_if_snapshot(interfaces);
 for(unsigned i=1;i<count;i++)if(interfaces[i].ifindex==index){
     memcpy(output,interfaces[i].name,sizeof(interfaces[i].name));
     return 0;
 }
 return -ENODEV;
}
unsigned nas_lwip_guest_ipv4_ifconf(
    struct nas_lwip_guest_ipv4_if out[NAS_LWIP_GUEST_IF_LIMIT]) {
 if(!out)return 0;
 struct nas_lwip_guest_if interfaces[NAS_LWIP_GUEST_IF_LIMIT]={0};
 unsigned total=nas_lwip_guest_if_snapshot(interfaces),count=0;
 for(unsigned i=0;i<total;i++) {
     const struct nas_lwip_guest_if *iface=&interfaces[i];
     if(!iface->ipv4_host_order || (!iface->loopback &&
         !iface->prefix_len))continue;
     out[count]=(struct nas_lwip_guest_ipv4_if){
         .ifindex=iface->ifindex,
         .ipv4_host_order=iface->ipv4_host_order,
         .prefix_len=iface->prefix_len,.mtu=iface->mtu
     };
     memcpy(out[count].name,iface->name,sizeof(out[count].name));
     ++count;
 }
 return count;
}
/* Device/registry notifications only wake this one worker. Nothing ever
 * calls lwIP from the IRQ or the U-Boot driver remove path. */
static void wake_rx(void *ctx){
 struct nas_ksem *sem=ctx;
 (void)nas_ksem_post(sem);
}
static struct nas_guest_nic *owner_of(struct netif *netif){
 for(unsigned i=0;i<NAS_NETDEV_LIMIT;i++)
     if(&network.nic[i].ethernet.interface==netif)return &network.nic[i];
 return NULL;
}
/* The core alone owns default-route selection. A carrier-up netif without
 * a DHCP lease is not yet a usable IPv4 egress route. Prefer the first
 * genuinely addressed interface; DHCP completion/loss may change this
 * choice without any new driver attach or carrier event. */
static void choose_default_on_core(void){
 struct netif *choice=NULL;
 for(unsigned i=0;i<NAS_NETDEV_LIMIT;i++){
     struct nas_lwip_eth *e=&network.nic[i].ethernet;
     if(e->attached && e->device &&
        !nas_netdev_is_removing(e->device) &&
        netif_is_up(&e->interface) && netif_is_link_up(&e->interface) &&
        network.nic[i].dhcp_ipv4_host_order &&
        network.nic[i].dhcp_prefix_len){
         choice=&e->interface;break;
     }
 }
 netif_set_default(choice);
}
/* A driver first makes the NetDevice unavailable. Only tcpip_thread can
 * stop DHCP and unregister its netif; only after this releases the device
 * pin may a bus-unbind retry free the original U-Boot DMA and rings. */
static void detach_on_core(void *ctx){
 struct nas_guest_nic *nic=ctx;
 if(!nic)return;
 nic->pending_detach=false;
 if(nic->ethernet.attached && nic->ethernet.device){
     (void)nas_netdev_set_rx_wake(nic->ethernet.device,NULL,NULL);
     netif_set_status_callback(&nic->ethernet.interface,NULL);
     if(nas_lwip_eth_core_detach(&nic->ethernet)){
         nas_print("[LWIP] netif detach retained owner; will retry\n");
         return;
     }
 }
 nic->link_sync_queued=false;
 nic->rx_last_irq_sequence=0;
 nic->dhcp_ipv4_host_order=0;
 nic->dhcp_prefix_len=0;
 nic->name[0]=0;
 choose_default_on_core();
 (void)nas_ksem_post(&network.rx_wakeup);
}
static void request_detach(struct nas_guest_nic *nic){
 if(!nic || nic->pending_detach || !nic->ethernet.attached)return;
 nic->pending_detach=true;
 if(tcpip_try_callback(detach_on_core,nic)!=ERR_OK)
     nic->pending_detach=false; /* bounded mailbox: retry next RX wake */
}
/* This callback executes on the genuine upstream tcpip_thread. An IPv4
 * address exists only after DHCP supplied it on this specific interface. */
static void network_status(struct netif *netif){
 struct nas_guest_nic *nic=owner_of(netif);
 if(!nic)return;
 const ip4_addr_t *addr=netif_ip4_addr(netif);
 nic->dhcp_ipv4_host_order=netif_is_up(netif) &&
     netif_is_link_up(netif) && dhcp_supplied_address(netif) &&
     !ip4_addr_isany_val(*addr) ?
     lwip_ntohl(ip4_addr_get_u32(addr)) : 0;
 nic->dhcp_prefix_len=0;
 if(nic->dhcp_ipv4_host_order) {
     uint32_t mask=lwip_ntohl(ip4_addr_get_u32(netif_ip4_netmask(netif)));
     uint32_t rest=mask;
     uint8_t length=0;
     while(rest&UINT32_C(0x80000000)){length++;rest<<=1;}
     if(!rest)nic->dhcp_prefix_len=length;
 }
 choose_default_on_core();
 nas_print("[LWIP NETIF STATUS] up=");nas_dec(netif_is_up(netif));
 nas_print(" link=");nas_dec(netif_is_link_up(netif));
 nas_print(" dhcp=");nas_dec(dhcp_supplied_address(netif));
 nas_print(" ip=");nas_dec(ip4_addr1(addr));nas_print(".");
 nas_dec(ip4_addr2(addr));nas_print(".");
 nas_dec(ip4_addr3(addr));nas_print(".");
 nas_dec(ip4_addr4(addr));nas_print("\n");
 if(!nic->dhcp_ipv4_host_order)return;
 nas_print("[LWIP DHCP BOUND] addr=");
 nas_dec(ip4_addr1(addr));nas_print(".");
 nas_dec(ip4_addr2(addr));nas_print(".");
 nas_dec(ip4_addr3(addr));nas_print(".");
 nas_dec(ip4_addr4(addr));nas_print("\n");
}
static void reconcile_link_on_core(void *ctx){
 struct nas_guest_nic *nic=ctx;
 if(!nic)return;
 nic->link_sync_queued=false;
 if(!nic->ethernet.attached || !nic->ethernet.device ||
    nic->pending_detach || nas_netdev_is_removing(nic->ethernet.device))
     return;
 struct netif *netif=&nic->ethernet.interface;
 bool carrier=nas_netdev_link_up(nic->ethernet.device);
 if(carrier==netif_is_link_up(netif))return;
 if(carrier)netif_set_link_up(netif);
 else netif_set_link_down(netif);
 network_status(netif);
 choose_default_on_core();
}
static void request_link_reconcile(struct nas_guest_nic *nic){
 if(nic->link_sync_queued || nic->pending_detach ||
    !nic->ethernet.attached || !nic->ethernet.device ||
    nas_netdev_is_removing(nic->ethernet.device))return;
 if(nas_netdev_link_up(nic->ethernet.device)==
    netif_is_link_up(&nic->ethernet.interface))return;
 nic->link_sync_queued=true;
 if(tcpip_try_callback(reconcile_link_on_core,nic)!=ERR_OK)
     nic->link_sync_queued=false;
}
/* Called on tcpip_thread after discovery; retains exactly one standard
 * NetDevice pin through nas_lwip_eth_core_attach. Every failure rolls back
 * the would-be interface independently, without poisoning the other NICs. */
static void attach_on_core(void *ctx){
 struct nas_guest_nic *nic=ctx;
 if(!nic)return;
 nic->pending_attach=false;
 if(network.stop || !nic->name[0])return;
 if(nas_lwip_eth_core_attach(&nic->ethernet,nic->name,true)){
     nic->name[0]=0;return;
 }
 if(nas_netdev_set_rx_wake(nic->ethernet.device,
                           wake_rx,&network.rx_wakeup)){
     (void)nas_lwip_eth_core_detach(&nic->ethernet);
     nic->name[0]=0;
     nas_print("[LWIP] could not attach NIC notification\n");
     return;
 }
 /* Registering the NIC emits a one-time synthetic bootstrap notification
  * before the lwIP listener exists. Start the deferred-IRQ delta at that
  * published sequence so acceptance never counts startup as an ISR. */
 nic->rx_last_irq_sequence=__atomic_load_n(
     &nic->ethernet.device->rx_sequence,__ATOMIC_ACQUIRE);
 netif_set_status_callback(&nic->ethernet.interface,network_status);
 network_status(&nic->ethernet.interface);
 choose_default_on_core();
 nas_print("[LWIP] native ");nas_print(nic->name);
 nas_print(" attached; DHCP started\n");
 (void)nas_ksem_post(&network.rx_wakeup);
}
/* The actual registry is authoritative. A short-lived discovery pin only
 * reads a name and is released before handing off to tcpip_thread. */
static void discover_devices(void){
 for(unsigned ordinal=0;ordinal<NAS_NETDEV_LIMIT;ordinal++){
     struct nas_netdev *dev=nas_netdev_get_ordinal(ordinal);
     if(!dev)break;
     bool known=false;
     for(unsigned i=0;i<NAS_NETDEV_LIMIT;i++)
         if(network.nic[i].name[0] &&
            !strcmp(network.nic[i].name,dev->name)){known=true;break;}
     if(!known){
         size_t n=strnlen(dev->name,NAS_LWIP_DEVICE_NAME_MAX);
         if(n && n<NAS_LWIP_DEVICE_NAME_MAX){
             for(unsigned i=0;i<NAS_NETDEV_LIMIT;i++){
                 struct nas_guest_nic *nic=&network.nic[i];
                 if(nic->name[0] || nic->ethernet.attached ||
                    nic->pending_attach || nic->pending_detach)continue;
                 memcpy(nic->name,dev->name,n+1U);
                 nic->pending_attach=true;
                 if(tcpip_try_callback(attach_on_core,nic)!=ERR_OK){
                     nic->pending_attach=false;nic->name[0]=0;
                 }
                 break;
             }
         }
     }
     (void)nas_netdev_put(dev);
 }
}
/* One bounded native worker handles every NIC. It never touches the lwIP
 * netif list directly; status, DHCP, default routes and detach are core
 * callbacks. Full RX budgets yield so no NIC starves other tasks. */
static void receive_worker(void *ctx){
 (void)ctx;
 if(!nas_ksem_wait(&network.rx_wakeup))return;
 while(!network.stop){
     discover_devices();
     bool saturated=false;
     bool needs_fallback=false;
     for(unsigned i=0;i<NAS_NETDEV_LIMIT;i++){
         struct nas_guest_nic *nic=&network.nic[i];
         if(!nic->ethernet.attached || !nic->ethernet.device ||
            nic->pending_detach)continue;
         if(nas_netdev_is_removing(nic->ethernet.device)){
             request_detach(nic);continue;
         }
         /* An IRQ-capable device delivers an event whenever its VirtIO
          * receive ring gains used descriptors. Legacy/mock devices without
          * a bound controller route remain on the historical timer path. */
         if(!nic->ethernet.device->rx_irq_driven)needs_fallback=true;
         uint64_t sequence=__atomic_load_n(
             &nic->ethernet.device->rx_sequence,__ATOMIC_ACQUIRE);
         if(nic->ethernet.device->rx_irq_driven &&
            sequence!=nic->rx_last_irq_sequence){
             nic->rx_last_irq_sequence=sequence;
             ++network.rx_deferred_drains;
         }
         request_link_reconcile(nic);
         int received=nas_lwip_eth_poll(&nic->ethernet,NAS_NET_RX_BUDGET);
         if(received==-EBUSY || received==-EAGAIN ||
            received==-ENOBUFS || received==-ENOMEM)continue;
         if(received<0){
             /* A permanently poisoned transport cannot be rediscovered
              * as a fresh netif while the same driver incarnation lives.
              * Quarantine it until the bus owner retires and re-registers. */
             (void)nas_netdev_begin_remove(nic->ethernet.device);
             nas_print("[LWIP] NIC RX failed; quarantining and detaching its netif\n");
             request_detach(nic);continue;
         }
         if(received==(int)NAS_NET_RX_BUDGET)saturated=true;
     }
     if(saturated){
         (void)nas_kthread_yield();
         continue;
     }
     report_real_net_irq(!needs_fallback);
     /* Exactly the same worker services both classes. For true hardware
      * notifications do NOT wake every 10ms to poll an empty queue; a
      * legacy non-IRQ NIC still needs that bounded compatibility fallback. */
     enum nas_kwait_result result;
     if(needs_fallback){
         const uint64_t deadline=nas_time_add_deadline(
             nas_arch_monotonic_ns(),NAS_NET_FALLBACK_POLL_NS);
         result=nas_ksem_wait_until(&network.rx_wakeup,deadline);
         if(result==NAS_KWAIT_TIMED_OUT)++network.rx_fallback_polls;
     }else{
         result=nas_ksem_wait(&network.rx_wakeup) ?
                NAS_KWAIT_READY : NAS_KWAIT_INVALID;
     }
     if(result==NAS_KWAIT_INVALID){
         network.stop=true;
         for(unsigned i=0;i<NAS_NETDEV_LIMIT;i++)
             request_detach(&network.nic[i]);
     }
 }
}
/* Upstream has initialized its own loopif. Start dynamic discovery only
 * after the core thread is ready to accept attach/detach callbacks. */
static void tcpip_ready(void *unused){
 (void)unused;
 network.ready=true;
 (void)nas_ksem_post(&network.rx_wakeup);
}
bool nas_lwip_guest_boot(void){
 if(network.ready || network.starting)return false;
#if defined(NAS_ENABLE_VIRTIO_BLK) || defined(NAS_ENABLE_VIRTIO_NET) || defined(NAS_ENABLE_VIRTIO_INPUT) || defined(NAS_ENABLE_VIRTIO_GPU)
 if(nas_platform_virtio_blk_probe()<0)return false;
#endif
 nas_ksem_init(&network.rx_wakeup,0);
 if(nas_netdev_set_change_wake(wake_rx,&network.rx_wakeup))return false;
 network.starting=true;
 sys_init();
 tcpip_init(tcpip_ready,NULL);
 if(!nas_kthread_spawn(receive_worker,NULL,&network.rx_handle)){
     (void)nas_netdev_set_change_wake(NULL,NULL);
     return false;
 }
 /* The asynchronous Netconn File worker is independent of physical NIC
  * inventory; native lwIP loopback is available even with no NIC. */
 if(!nas_lwip_bridge_start()){
     network.stop=true;
     (void)nas_netdev_set_change_wake(NULL,NULL);
     (void)nas_ksem_post(&network.rx_wakeup);
     return false;
 }
 nas_print("[LWIP] official TCP/IP thread scheduled; native loopback enabled\n");
 return true;
}
