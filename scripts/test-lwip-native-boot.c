/* Hosted NaSeed boot wiring: native scheduler runs official tcpip thread,
 * binds a real netdev identity and pumps RX outside IRQ context. */
#include "../kernel/task/kthread.h"
#include "../kernel/fd.h"
#include "../kernel/drivers/port/net.h"
#include "../kernel/drivers/core/bus.h"
#include "../ports/lwip/network_boot.h"
#include "../ports/lwip/netconn_bridge.h"
#include "../kernel/errno.h"
#include "lwip/sys.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/prot/dhcp.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern bool nas_lwip_guest_boot(void);
static uint64_t now_ns;
static unsigned scans,transmitted,polls,delivered;
static unsigned second_transmitted,second_polls,second_delivered;
static int next_poll_error;
static bool inbound_pending=true;
static bool carrier=true;
static bool second_inbound_pending=true,second_carrier=true;
static struct nas_netdev second;
uint64_t nas_arch_monotonic_ns(void){return now_ns;}
void nas_print(const char *s){fputs(s,stdout);}
void nas_dec(uint64_t n){printf("%llu",(unsigned long long)n);}
__attribute__((noreturn)) void nas_arch_halt(void){abort();}
void *nas_heap_calloc(size_t n,size_t size){return calloc(n,size);}
bool nas_heap_free(void *p){free(p);return true;}
struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];
int nas_platform_virtio_blk_probe(void){scans++;return 0;}

static int transmit(void *ctx,const uint8_t *frame,uint32_t bytes){
 assert(bytes>=14 && bytes<=1518);
 assert(frame[12]==0x08 && frame[13]==0x00); /* real DHCP IPv4 frame */
 if(ctx==&second)second_transmitted++;
 else transmitted++;
 return 0;
}
static int poll(void *ctx,uint32_t budget,nas_netdev_rx receive,void *consumer){
 assert(budget==8);
 if(ctx==&second){
     second_polls++;
     if(!second_inbound_pending)return 0;
     uint8_t frame[60]={0xff,0xff,0xff,0xff,0xff,0xff,
                        0x52,0x54,0,0x12,0x34,0x57,0x08,0x06};
     int rc=receive(consumer,frame,sizeof(frame));
     if(rc)return rc;
     second_inbound_pending=false;second_delivered++;
     return 1;
 }
 polls++;
 if(next_poll_error){int rc=next_poll_error;next_poll_error=0;return rc;}
 if(!inbound_pending)return 0;
 uint8_t frame[60]={0xff,0xff,0xff,0xff,0xff,0xff,
                    0x52,0x54,0,0x12,0x34,0x56,0x08,0x06};
 int rc=receive(consumer,frame,sizeof(frame));
 if(rc)return rc;
 inbound_pending=false;
 delivered++;
 return 1;
}
static bool link_up(void *ctx){return ctx==&second?second_carrier:carrier;}
static const struct nas_netdev_ops network_ops={
 .transmit=transmit,.poll=poll,.link_up=link_up
};
static struct nas_netdev eth={
 .name="en0",.mac={0x52,0x54,0,0x12,0x34,0x56},
 .mtu=1500,.backend=&eth,.ops=&network_ops
};
static struct nas_netdev second={
 .name="en1",.mac={0x52,0x54,0,0x12,0x34,0x57},
 .mtu=1500,.backend=&second,.ops=&network_ops
};
/* The fake PCI discoverer owns transport lifetime while the genuine lwIP
 * core drops the final pin. This exercises the ACTUAL generic bus reaper. */
static struct nas_bus_device second_bus;
static uint8_t second_pci_config[NAS_BUS_PCI_CONFIG_BYTES];
static bool second_bus_match(const struct nas_bus_device *dev){
 return dev==&second_bus;
}
static int second_bus_probe(struct nas_bus_device *dev){
 return dev==&second_bus && second.registered ? 0:-ENODEV;
}
static int second_bus_remove(struct nas_bus_device *dev){
 if(dev!=&second_bus)return -ENODEV;
 int rc=nas_netdev_begin_remove(&second);
 return rc ? rc:nas_netdev_unregister(&second);
}
static const struct nas_bus_driver second_driver={
 .name="native-lwip-device-lifetime",
 .bus_mask=NAS_BUS_MASK_PCI,.match=second_bus_match,
 .probe=second_bus_probe,.remove=second_bus_remove
};
static struct nas_bus_device second_bus={
 .bus=NAS_BUS_PCI,.device_id=1,
 .resource={.kind=NAS_BUS_RESOURCE_PCI,
            .view.pci={.config=second_pci_config,
                       .config_bytes=NAS_BUS_PCI_CONFIG_BYTES,
                       .mapped_bar_mask=1}}
};
/* The fake Ethernet driver has no DHCP server. Inject a lease transition
 * on the real upstream tcpip_thread, exercising production netif/status
 * callbacks and default-route ownership without a second network stack. */
struct lease_change {
 unsigned ifindex;
 uint32_t host_ip;
 uint32_t dns_host_order[NAS_LWIP_DNS_SERVER_LIMIT];
 bool complete;
};
static void change_lease_on_core(void *arg){
 struct lease_change *change=arg;
 struct netif *interface=netif_get_by_index((u8_t)change->ifindex);
 assert(interface);
 struct dhcp *dhcp=netif_dhcp_data(interface);
 assert(dhcp);
 dhcp->state=change->host_ip?DHCP_STATE_BOUND:DHCP_STATE_OFF;
 ip4_addr_t ip={0},mask={0},gateway={0};
 if(change->host_ip){
     const uint32_t v=change->host_ip;
     IP4_ADDR(&ip,v>>24,(v>>16)&255U,(v>>8)&255U,v&255U);
     IP4_ADDR(&mask,255,255,255,0);
     IP4_ADDR(&gateway,v>>24,(v>>16)&255U,(v>>8)&255U,2);
     /* Mimic upstream DHCP's option-6 ACK side effect before dhcp_bind.
      * Real core code must keep this last-ACK-wins DNS global per NIC. */
     for(unsigned i=0;i<NAS_LWIP_DNS_SERVER_LIMIT;i++) {
         ip_addr_t server;
         uint32_t dns=change->dns_host_order[i];
         IP_ADDR4(&server,dns>>24,(dns>>16)&255U,(dns>>8)&255U,dns&255U);
         dns_setserver((u8_t)i,&server);
     }
 }
 netif_set_addr(interface,&ip,&mask,&gateway);
 change->complete=true;
}
static void change_lease_dns(unsigned ifindex,uint32_t host_ip,
                             uint32_t dns0,uint32_t dns1){
 struct lease_change change={.ifindex=ifindex,.host_ip=host_ip,
                             .dns_host_order={dns0,dns1}};
 assert(tcpip_try_callback(change_lease_on_core,&change)==ERR_OK);
 for(unsigned attempt=0;attempt<100 && !change.complete;attempt++)
     assert(nas_kthread_run(1)==1);
 assert(change.complete);
}
static void change_lease(unsigned ifindex,uint32_t host_ip){
 change_lease_dns(ifindex,host_ip,host_ip ?
                  (host_ip & UINT32_C(0xffffff00)) | 3U : 0,0);
}
static void assert_dns(uint32_t primary,uint32_t secondary){
 uint32_t snapshot[NAS_LWIP_DNS_SERVER_LIMIT]={UINT32_MAX,UINT32_MAX};
 unsigned count=nas_lwip_guest_dns_snapshot(snapshot);
 assert(count==(primary ? secondary ? 2U : 1U : 0U));
 assert(snapshot[0]==primary && snapshot[1]==secondary);
}
int main(void){
 nas_kthread_system_init();
 assert(nas_netdev_register(&eth)==0);
 assert(nas_lwip_guest_boot());
 assert(scans==1 && nas_kthread_live()==3); /* TCP/IP, NIC RX, Netconn RPC */
 for(unsigned attempt=0;attempt<60 &&
       (eth.refs!=1 || !transmitted);attempt++)
     assert(nas_kthread_run(1)==1); /* tcpip, worker discovery, DHCP */
 assert(transmitted>=1 && eth.refs==1);
 assert(nas_netdev_unregister(&eth)!=0); /* lwIP pins the live device */
 struct nas_lwip_guest_if devices[NAS_LWIP_GUEST_IF_LIMIT]={0};
 assert(nas_lwip_guest_if_snapshot(devices)==2);
 assert(!strcmp(devices[0].name,"lo") && devices[0].loopback &&
        devices[0].ifindex==1 && devices[0].ipv4_host_order==0x7f000001U);
 assert(!strcmp(devices[1].name,"en0") && !devices[1].loopback &&
        devices[1].up && devices[1].link_up && devices[1].mtu==1500 &&
        devices[1].ifindex>1);
 assert(!netif_default); /* a live carrier without DHCP is not IPv4 egress */
 assert_dns(0,0);
 for(unsigned i=0;i<10 && !delivered;i++)assert(nas_kthread_run(1)==1);
 assert(delivered==1 && polls>=1); /* worker, outside tcpip stack */
 assert(nas_kthread_run(1)==1); /* upstream lwIP consumes inbound frame */
 inbound_pending=true;
 nas_netdev_notify_rx(&eth); /* IRQ path only wakes, no RX parsing */
 assert(delivered==1);
 for(unsigned i=0;i<4 && delivered<2;i++)
     assert(nas_kthread_run(1)==1);
 assert(delivered==2);
 now_ns+=UINT64_C(11000000);
 assert(nas_kthread_timers_tick(now_ns)>=1);
 for(unsigned i=0;i<4 && polls<3;i++)
     assert(nas_kthread_run(1)==1); /* user-scheduler slices may interleave */
 assert(polls>=3);
 /* A real driver can hold the shared TX/RX owner or exhaust pbufs. Neither
  * event is permanent NIC failure, and both must preserve the live netif. */
 const int transient_errors[]={-EBUSY,-ENOBUFS,-ENOMEM};
 for(unsigned error=0;error<sizeof(transient_errors)/sizeof(transient_errors[0]);error++){
     const unsigned previous=polls;
     next_poll_error=transient_errors[error];
     nas_netdev_notify_rx(&eth);
     for(unsigned attempt=0;attempt<8 && polls==previous;attempt++)
         assert(nas_kthread_run(1)==1);
     assert(polls>previous && next_poll_error==0);
     assert(eth.registered && eth.refs==1 && eth.rx_wake);
 }
 /* Carrier transitions belong to the driver; lwIP link state and DHCP
  * reconfiguration happen exclusively on tcpip_thread. No address is ever
  * fabricated for the detached link, but the device identity remains. */
 carrier=false;
 nas_netdev_notify_rx(&eth);
 bool saw_down=false;
 for(unsigned attempt=0;attempt<30;attempt++) {
     assert(nas_kthread_run(1)==1);
     assert(nas_lwip_guest_if_snapshot(devices)==2);
     if(!devices[1].link_up){saw_down=true;break;}
 }
 assert(saw_down && devices[1].up && !devices[1].ipv4_host_order &&
        devices[1].mtu==1500);
 struct nas_lwip_guest_ipv4_if addressed[NAS_LWIP_GUEST_IF_LIMIT]={0};
 assert(nas_lwip_guest_ipv4_ifconf(addressed)==1 &&
        !strcmp(addressed[0].name,"lo"));
 carrier=true;
 nas_netdev_notify_rx(&eth);
 bool saw_up=false;
 for(unsigned attempt=0;attempt<30;attempt++) {
     assert(nas_kthread_run(1)==1);
     assert(nas_lwip_guest_if_snapshot(devices)==2);
     if(devices[1].link_up){saw_up=true;break;}
 }
 assert(saw_up && devices[1].up && devices[1].mtu==1500 &&
        devices[1].ifindex>1);
 /* Linux names the physical device en0; upstream lwIP has a distinct
  * internal netif index. Never claim that another name is this interface. */
 int real_ifindex=nas_lwip_guest_ifindex("en0");
 assert(real_ifindex>0 && real_ifindex<=255);
 assert(nas_lwip_guest_ifindex("eth0")==-ENODEV);
 char bound_name[NAS_LWIP_DEVICE_NAME_MAX]={0};
 assert(nas_lwip_guest_ifname((unsigned)real_ifindex,bound_name)==0);
 assert(!strcmp(bound_name,"en0"));
 assert(nas_lwip_guest_ifname((unsigned)real_ifindex+1,bound_name)==-ENODEV);
 struct nas_lwip_bridge_id bound={0};
 assert(nas_lwip_bridge_open(40,NAS_LWIP_BRIDGE_UDP,&bound)==0);
 struct nas_lwip_bridge_result bound_result={0};
 bool opened=false;
 for(unsigned attempt=0;attempt<60;attempt++) {
     int got=nas_lwip_bridge_take(bound,&bound_result);
     if(got==1){opened=true;break;}
     assert(got==0 && nas_kthread_run(1)==1);
 }
 assert(opened && bound_result.status==0);
 assert(nas_lwip_bridge_submit(bound,&(struct nas_lwip_bridge_request){
     .op=NAS_LWIP_BRIDGE_BIND_IF,.if_index=(uint8_t)real_ifindex})==0);
 bool bound_done=false;
 for(unsigned attempt=0;attempt<60;attempt++) {
     int got=nas_lwip_bridge_take(bound,&bound_result);
     if(got==1){bound_done=true;break;}
     assert(got==0 && nas_kthread_run(1)==1);
 }
 assert(bound_done && bound_result.status==0);
 assert(nas_lwip_bridge_release(bound)==0);

 /* The second device arrives AFTER stack boot. Discovery uses the sole
  * NetDevice registry, attaches a separate netif on tcpip_thread and never
  * steals the first device's route merely by being attached later. */
 assert(nas_netdev_register(&second)==0);
 for(unsigned attempt=0;attempt<100 &&
       (second.refs!=1 || !second_transmitted);attempt++)
     assert(nas_kthread_run(1)==1);
 assert(second.refs==1 && second_transmitted>=1);
 assert(nas_lwip_guest_if_snapshot(devices)==3);
 assert(!strcmp(devices[2].name,"en1") && devices[2].ifindex>1 &&
        devices[2].ifindex!=(unsigned)real_ifindex && devices[2].link_up);
 const unsigned second_index=devices[2].ifindex;
 assert(nas_lwip_guest_ifindex("en1")== (int)second_index);
 assert(nas_lwip_guest_ifname(second_index,bound_name)==0 &&
        !strcmp(bound_name,"en1"));
 assert(!netif_default); /* neither DHCP client has a lease yet */
 assert_dns(0,0);
 /* The first NIC may never receive DHCP while the later NIC does.
  * An actual lease, not registration order, must select the IPv4 route. */
 change_lease(second_index,UINT32_C(0x0a000314));
 assert(netif_default && netif_get_index(netif_default)==second_index);
 assert_dns(UINT32_C(0x0a000303),0);
 struct nas_lwip_guest_ipv4_if addressed_second[NAS_LWIP_GUEST_IF_LIMIT]={0};
 assert(nas_lwip_guest_ipv4_ifconf(addressed_second)==2 &&
        !strcmp(addressed_second[1].name,"en1") &&
        addressed_second[1].ifindex==second_index);
 /* A delayed first lease can take priority; losing its IPv4 address,
  * without losing carrier, must fall back to the still-leased sibling. */
 change_lease((unsigned)real_ifindex,UINT32_C(0x0a00020f));
 assert(netif_default && netif_get_index(netif_default)==(unsigned)real_ifindex);
 assert_dns(UINT32_C(0x0a000203),0);
 /* An ACK from en1 must not leak into DNS owned by the default en0.
  * The unchanged-address ACK path also exercises lwIP's extended event. */
 change_lease_dns(second_index,UINT32_C(0x0a000314),
                  UINT32_C(0x0a000335),UINT32_C(0xc0000235));
 assert_dns(UINT32_C(0x0a000203),0);
 change_lease_dns((unsigned)real_ifindex,UINT32_C(0x0a00020f),
                  UINT32_C(0x0a000235),UINT32_C(0xc6336435));
 assert_dns(UINT32_C(0x0a000235),UINT32_C(0xc6336435));
 change_lease((unsigned)real_ifindex,0);
 assert(netif_default && netif_get_index(netif_default)==second_index);
 assert_dns(UINT32_C(0x0a000335),UINT32_C(0xc0000235));
 assert(nas_lwip_guest_ipv4_ifconf(addressed_second)==2 &&
        !strcmp(addressed_second[1].name,"en1"));
 change_lease((unsigned)real_ifindex,UINT32_C(0x0a00020f));
 assert(netif_default && netif_get_index(netif_default)==(unsigned)real_ifindex);
 assert_dns(UINT32_C(0x0a000203),0);
 for(unsigned attempt=0;attempt<20 && !second_delivered;attempt++)
     assert(nas_kthread_run(1)==1);
 assert(second_delivered==1 && second_polls>=1);

 /* The second NIC loses carrier, then gets it back independently of en0. */
 second_carrier=false;nas_netdev_notify_rx(&second);
 bool second_down=false;
 for(unsigned attempt=0;attempt<40;attempt++){
     assert(nas_kthread_run(1)==1);
     if(nas_lwip_guest_if_snapshot(devices)==3 && !devices[2].link_up){
         second_down=true;break;
     }
 }
 assert(second_down && devices[1].link_up && !devices[2].ipv4_host_order);
 second_carrier=true;nas_netdev_notify_rx(&second);
 bool second_up=false;
 for(unsigned attempt=0;attempt<40;attempt++){
     assert(nas_kthread_run(1)==1);
     if(nas_lwip_guest_if_snapshot(devices)==3 && devices[2].link_up){
         second_up=true;break;
     }
 }
 assert(second_up && devices[1].link_up);

 /* Generic bus remove begins immediately, then waits for genuine upstream
  * tcpip_thread to detach the lwIP netif. No caller-driven unbind retry. */
 assert(nas_bus_driver_register(&second_driver)==0);
 assert(nas_bus_device_register(&second_bus)==0);
 assert(nas_bus_device_request_remove(&second_bus)==0);
 assert(nas_bus_device_remove_status(&second_bus)==-EINPROGRESS);
 assert(nas_netdev_get("en1")==NULL);
 assert(nas_bus_service_removals(1)==1 && second_bus.registered);
 for(unsigned attempt=0;attempt<100 && second.refs;attempt++)
     assert(nas_kthread_run(1)==1);
 assert(second.refs==0 && !second.rx_wake);
 assert(nas_lwip_guest_if_snapshot(devices)==2);
 assert(nas_lwip_guest_ifindex("en1")==-ENODEV);
 assert(nas_bus_service_removals(1)==1);
 assert(nas_bus_device_remove_status(&second_bus)==0 &&
        !second_bus.registered && !second.registered);

 /* The same stable driver storage may register another incarnation;
  * stale netif indexes or old IRQ callbacks must never survive. */
 assert(nas_netdev_register(&second)==0);
 assert(nas_bus_device_register(&second_bus)==0);
 for(unsigned attempt=0;attempt<100 && second.refs!=1;attempt++)
     assert(nas_kthread_run(1)==1);
 assert(second.refs==1 && nas_lwip_guest_if_snapshot(devices)==3);
 assert(nas_lwip_guest_ifindex("en1")==(int)devices[2].ifindex);
 change_lease(devices[2].ifindex,UINT32_C(0x0a000314));
 assert(netif_default && netif_get_index(netif_default)==(unsigned)real_ifindex);

 /* A fatal RX error for en0 detaches ONLY en0; the second NIC must stay
  * pinned and become the new default route when the first disappears. */
 next_poll_error=-EIO;nas_netdev_notify_rx(&eth);
 for(unsigned attempt=0;attempt<100 && eth.refs;attempt++)
     assert(nas_kthread_run(1)==1);
 assert(!eth.refs && !eth.rx_wake && second.refs==1);
 assert(nas_netdev_is_removing(&eth) && nas_netdev_get("en0")==NULL);
 assert(nas_lwip_guest_if_snapshot(devices)==2);
 assert(!strcmp(devices[1].name,"en1"));
 assert(netif_default && netif_get_index(netif_default)==devices[1].ifindex);
 assert_dns(UINT32_C(0x0a000303),0);
 assert(nas_lwip_guest_ifindex("en0")==-ENODEV);
 assert(nas_netdev_unregister(&eth)==0);
 assert(nas_bus_device_request_remove(&second_bus)==0);
 for(unsigned attempt=0;attempt<100 && second.refs;attempt++)
     assert(nas_kthread_run(1)==1);
 assert(!second.refs && nas_bus_service_removals(1)==1);
 assert(nas_bus_device_remove_status(&second_bus)==0);
 assert(nas_bus_driver_unregister(&second_driver)==0);
 assert(nas_lwip_guest_if_snapshot(devices)==1);
 assert_dns(0,0);

 puts("[LWIP NATIVE BOOT PASS] two live NICs, late attach, independent DHCP/link,");
 puts("  delayed/withdrawn DHCP, route+DNS fallback, unchanged-IP renew, bounded RX, bus-owned async detach/re-attach, pins");
}
