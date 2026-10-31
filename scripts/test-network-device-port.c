#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/drivers/port/net.h"
#include "../kernel/errno.h"

static unsigned tx_count,rx_count;
static int send(void *context,const uint8_t *data,uint32_t size){
    assert(context==(void *)&tx_count && data && size==60);
    tx_count++;
    return 0;
}
static int incoming(void *context,uint32_t budget,nas_netdev_rx receive,
                    void *consumer){
    assert(context==(void *)&tx_count && budget==4);
    const uint8_t frame[60]={0};
    assert(receive(consumer,frame,sizeof(frame))==0);
    return 1;
}
static int on_frame(void *consumer,const uint8_t *frame,uint32_t bytes){
    assert(consumer==(void *)&rx_count && frame && bytes==60);
    rx_count++;
    return 0;
}
static bool up(void *context){return context==(void *)&tx_count;}
static const struct nas_netdev_ops ops={
    .transmit=send,.poll_receive=incoming,.link_up=up};
static struct nas_netdev first={
    .name="eth0",.mac={0x52,0x54,0,0x12,0x34,0x56},
    .mtu=1500,.backend=&tx_count,.ops=&ops};
static struct nas_netdev duplicate={
    .name="eth0",.mac={0x52,0x54,0,0x12,0x34,0x57},
    .mtu=1500,.backend=&tx_count,.ops=&ops};
int main(void){
    assert(nas_netdev_register(&first)==0);
    assert(nas_netdev_register(&first)==-EINVAL);
    assert(nas_netdev_register(&duplicate)==-EEXIST);
    assert(nas_netdev_get("eth1")==NULL);
    struct nas_netdev *pin=nas_netdev_get("eth0");
    assert(pin==&first && first.refs==1);
    assert(nas_netdev_link_up(pin));
    uint8_t frame[60]={0};
    assert(nas_netdev_transmit(pin,frame,sizeof(frame))==0 && tx_count==1);
    assert(nas_netdev_transmit(pin,frame,13)==-EMSGSIZE);
    assert(nas_netdev_transmit(pin,frame,1519)==-EMSGSIZE);
    assert(nas_netdev_poll_receive(pin,4,on_frame,&rx_count)==1 && rx_count==1);
    assert(nas_netdev_unregister(&first)==-EBUSY);
    assert(nas_netdev_put(pin)==0);
    assert(nas_netdev_put(pin)==-EINVAL);
    assert(nas_netdev_unregister(&first)==0);
    assert(!nas_netdev_link_up(&first));
    assert(nas_netdev_transmit(&first,frame,sizeof(frame))==-ENODEV);
    duplicate.mac[0]=1;
    assert(nas_netdev_register(&duplicate)==-EINVAL);
    puts("[NET DEVICE PORT PASS] Ethernet TX/RX boundary, MAC, pin/unpin and safe detach");
    return 0;
}
