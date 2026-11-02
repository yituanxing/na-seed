#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/drivers/port/net.h"
#include "../kernel/errno.h"

static unsigned tx_count,rx_count,wake_count;
static unsigned registry_events,registry_rebound_events,wake_rebound_count;
static void changed(void *context){
    assert(context==&registry_events);
    registry_events++;
}
static void changed_rebound(void *context){
    assert(context==&registry_rebound_events);
    registry_rebound_events++;
}
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
static void wake(void *context){
    assert(context==(void *)&wake_count);
    wake_count++;
}
static void wake_rebound(void *context){
    assert(context==(void *)&wake_rebound_count);
    wake_rebound_count++;
}
static const struct nas_netdev_ops ops={
    .transmit=send,.poll=incoming,.link_up=up};
static struct nas_netdev first={
    .name="eth0",.mac={0x52,0x54,0,0x12,0x34,0x56},
    .mtu=1500,.backend=&tx_count,.ops=&ops};
static struct nas_netdev duplicate={
    .name="eth0",.mac={0x52,0x54,0,0x12,0x34,0x57},
    .mtu=1500,.backend=&tx_count,.ops=&ops};
static struct nas_netdev alternate={
    .name="en0",.mac={0x52,0x54,0,0x12,0x34,0x58},
    .mtu=1500,.backend=&tx_count,.ops=&ops};
int main(void){
    assert(nas_netdev_set_change_wake(changed,&registry_events)==0);
    assert(nas_netdev_set_change_wake(changed,&registry_events)==-EBUSY);
    assert(nas_netdev_register(&first)==0);
    assert(nas_netdev_register(&first)==-EINVAL);
    assert(nas_netdev_register(&duplicate)==-EEXIST);
    assert(nas_netdev_register(&alternate)==0);
    assert(nas_netdev_get("eth1")==NULL);
    struct nas_netdev *other=nas_netdev_get_ordinal(1);
    assert(other==&alternate && alternate.refs==1);
    assert(nas_netdev_get_ordinal(2)==NULL);
    assert(nas_netdev_unregister(&alternate)==-EBUSY);
    assert(nas_netdev_put(other)==0);
    assert(nas_netdev_get_ordinal(0)==&first && first.refs==1);
    assert(nas_netdev_put(&first)==0);
    struct nas_netdev *pin=nas_netdev_get("eth0");
    assert(pin==&first && first.refs==1);
    assert(nas_netdev_set_rx_wake(pin,wake,&wake_count)==0);
    assert(nas_netdev_set_rx_wake(pin,wake,&wake_count)==-EBUSY);
    nas_netdev_notify_rx(pin);
    nas_netdev_notify_rx(pin);
    assert(wake_count==2 && pin->rx_sequence==2);
    assert(nas_netdev_link_up(pin));
    uint8_t frame[60]={0};
    assert(nas_netdev_transmit(pin,frame,sizeof(frame))==0 && tx_count==1);
    assert(nas_netdev_transmit(pin,frame,13)==-EMSGSIZE);
    assert(nas_netdev_transmit(pin,frame,1519)==-EMSGSIZE);
    assert(nas_netdev_poll(pin,4,on_frame,&rx_count)==1 && rx_count==1);
    assert(nas_netdev_unregister(&first)==-EBUSY);
    assert(nas_netdev_put(pin)==-EBUSY); /* cannot leave an IRQ callback */
    assert(nas_netdev_set_rx_wake(pin,NULL,NULL)==0);
    assert(pin->rx_wake==NULL && pin->rx_wake_context==NULL);
    assert(nas_netdev_set_rx_wake(pin,wake_rebound,&wake_rebound_count)==0);
    nas_netdev_notify_rx(pin);
    assert(wake_rebound_count==1 && wake_count==2);
    assert(nas_netdev_set_rx_wake(pin,NULL,NULL)==0);
    assert(pin->rx_wake==NULL && pin->rx_wake_context==NULL);
    assert(nas_netdev_put(pin)==0);
    assert(nas_netdev_put(pin)==-EINVAL);
    assert(nas_netdev_unregister(&first)==0);
    assert(nas_netdev_get_ordinal(0)==&alternate);
    assert(nas_netdev_put(&alternate)==0);
    /* A real driver makes the object invisible to new consumers before
     * freeing any DMA. Its attached worker receives one last wake and must
     * release the old pin before unregister can finish. */
    struct nas_netdev *drain=nas_netdev_get("en0");
    assert(drain==&alternate);
    assert(nas_netdev_set_rx_wake(drain,wake,&wake_count)==0);
    assert(nas_netdev_begin_remove(drain)==0);
    assert(nas_netdev_begin_remove(drain)==0); /* idempotent */
    assert(nas_netdev_is_removing(drain) && !nas_netdev_link_up(drain));
    assert(nas_netdev_get("en0")==NULL && nas_netdev_get_ordinal(0)==NULL);
    assert(nas_netdev_transmit(drain,frame,sizeof(frame))==-ENODEV);
    assert(nas_netdev_poll(drain,4,on_frame,&rx_count)==-ENODEV);
    assert(nas_netdev_set_rx_wake(drain,wake,&wake_count)==-ENODEV);
    assert(nas_netdev_unregister(drain)==-EBUSY);
    assert(wake_count==3); /* unregister request wakes exactly once */
    assert(nas_netdev_set_rx_wake(drain,NULL,NULL)==0);
    assert(nas_netdev_put(drain)==0);
    assert(nas_netdev_unregister(&alternate)==0);
    assert(nas_netdev_get_ordinal(0)==NULL);
    nas_netdev_notify_rx(&first);
    assert(wake_count==3);
    assert(!nas_netdev_link_up(&first));
    assert(nas_netdev_transmit(&first,frame,sizeof(frame))==-ENODEV);
    duplicate.mac[0]=1;
    assert(nas_netdev_register(&duplicate)==-EINVAL);
    assert(registry_events==5); /* two register, two remove, one drain */
    assert(nas_netdev_set_change_wake(NULL,NULL)==0);
    assert(nas_netdev_set_change_wake(changed_rebound,
                                      &registry_rebound_events)==0);
    assert(nas_netdev_register(&first)==0);
    assert(nas_netdev_unregister(&first)==0);
    assert(registry_rebound_events==2 && registry_events==5);
    assert(nas_netdev_set_change_wake(NULL,NULL)==0);
    puts("[NET DEVICE PORT PASS] bounded poll, callback/context rebind, remove/drain, DMA pins");
    return 0;
}
