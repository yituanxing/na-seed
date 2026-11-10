#include "netif_port.h"
#include "../../kernel/errno.h"
#include "../../kernel/lib/string.h"
#include "lwip/etharp.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"
#include "lwip/dhcp.h"
#include <stdint.h>
#include <stddef.h>

#define NAS_LWIP_ETH_FRAME_MAX 1518U
static err_t transmit_frame(struct netif *netif,struct pbuf *p){
 struct nas_lwip_eth *e=netif?netif->state:NULL;
 if(!e || !e->attached || !e->device || !p ||
    p->tot_len<14U || p->tot_len>NAS_LWIP_ETH_FRAME_MAX)
     return ERR_IF;
 uint8_t bytes[NAS_LWIP_ETH_FRAME_MAX];
 if(pbuf_copy_partial(p,bytes,p->tot_len,0)!=p->tot_len)return ERR_IF;
 int rc=nas_netdev_transmit(e->device,bytes,p->tot_len);
 return rc==0?ERR_OK:(rc==-EBUSY || rc==-EAGAIN || rc==-ENOBUFS
                     ? ERR_MEM:ERR_IF);
}
static err_t initialize_netif(struct netif *netif){
 struct nas_lwip_eth *e=netif?netif->state:NULL;
 if(!e || !e->device)return ERR_ARG;
 netif->name[0]='n';netif->name[1]='s';
 netif->output=etharp_output;
 netif->linkoutput=transmit_frame;
 netif->mtu=e->device->mtu;
 netif->hwaddr_len=NAS_NETDEV_MAC_BYTES;
 memcpy(netif->hwaddr,e->device->mac,NAS_NETDEV_MAC_BYTES);
 netif->flags=NETIF_FLAG_BROADCAST|NETIF_FLAG_ETHARP|NETIF_FLAG_ETHERNET;
 return ERR_OK;
}
int nas_lwip_eth_core_attach(struct nas_lwip_eth *e,
                             const char *device_name,bool dhcp){
 if(!e || !device_name || e->attached || e->device)return -EINVAL;
 struct nas_netdev *dev=nas_netdev_get(device_name);
 if(!dev)return -ENODEV;
 e->device=dev;
 if(!netif_add(&e->interface,NULL,NULL,NULL,e,initialize_netif,tcpip_input)){
     e->device=NULL;(void)nas_netdev_put(dev);return -EIO;
 }
 e->attached=true;
 /* The network manager selects exactly one default route across all live
  * instances; a class adapter must not make the last attached NIC win. */
 netif_set_up(&e->interface);
 if(nas_netdev_link_up(dev))netif_set_link_up(&e->interface);
 if(dhcp && dhcp_start(&e->interface)!=ERR_OK){
     (void)nas_lwip_eth_core_detach(e);
     return -EIO;
 }
 return 0;
}
int nas_lwip_eth_core_detach(struct nas_lwip_eth *e){
 if(!e || !e->attached || !e->device)return -ENODEV;
 dhcp_stop(&e->interface);
 netif_set_link_down(&e->interface);
 netif_set_down(&e->interface);
 if(netif_default==&e->interface)netif_set_default(NULL);
 netif_remove(&e->interface);
 int rc=nas_netdev_put(e->device);
 if(rc)return rc;
 e->device=NULL;e->attached=false;
 return 0;
}
static int consume_frame(void *context,const uint8_t *frame,uint32_t bytes){
 struct nas_lwip_eth *e=context;
 if(bytes<14U || bytes>NAS_LWIP_ETH_FRAME_MAX)return -EMSGSIZE;
 struct pbuf *p=pbuf_alloc(PBUF_RAW,(u16_t)bytes,PBUF_POOL);
 if(!p)return -ENOMEM;
 if(pbuf_take(p,frame,(u16_t)bytes)!=ERR_OK){
     pbuf_free(p);return -EIO;
 }
 err_t result=tcpip_input(p,&e->interface);
 if(result!=ERR_OK){pbuf_free(p);return result==ERR_MEM?-ENOBUFS:-EIO;}
 return 0;
}
int nas_lwip_eth_poll(struct nas_lwip_eth *e,unsigned budget){
 if(!e || !e->attached || !e->device)return -ENODEV;
 return nas_netdev_poll(e->device,budget,consume_frame,e);
}
