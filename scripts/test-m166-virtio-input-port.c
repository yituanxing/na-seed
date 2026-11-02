/* Host ABI test for Linux virtio_input.c's NaSeed port. Uses the actual
 * upstream Unikraft vring and DMA adapter with a miniature QEMU-like device.
 * Hardware QMP injection, on all three native ISAs, remains a separate gate.
 */
#include "../kernel/drivers/input/virtio_input.h"
#include "../kernel/drivers/input/core.h"
#include "../kernel/drivers/core/bus.h"
#include "../kernel/drivers/virtio/transport.h"
#include "../ports/unikraft/split_queue.h"
#include "../kernel/errno.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static _Alignas(4096) uint8_t pages[12][NAS_DMA_PAGE_BYTES];
static bool page_used[12];
uint64_t nas_hhdm_offset(void) { return 0; }
uint64_t nas_frame_alloc(void) {
    for (unsigned i=0;i<12;i++) if (!page_used[i]) {
        page_used[i]=true;
        return (uint64_t)(uintptr_t)pages[i];
    }
    return 0;
}
bool nas_frame_put(uint64_t pa) {
    for (unsigned i=0;i<12;i++) if (page_used[i] &&
        pa==(uint64_t)(uintptr_t)pages[i]) {
        page_used[i]=false;return true;
    }
    return false;
}

struct fake_vq {unsigned count;struct vring_desc *desc;
                struct vring_avail *avail;struct vring_used *used;};
struct fake_input {uint32_t status;uint64_t features;
    uint8_t select,subsel;unsigned resets,notifies;bool tablet;
    struct fake_vq queues[2];};

static const struct nas_bus_driver *bound_driver;
int nas_bus_driver_register(const struct nas_bus_driver *driver) {
    assert(driver && !bound_driver);
    bound_driver=driver;return 0;
}
static unsigned registered;
static struct nas_input_device *received[2];
static struct {uint16_t type,code;int32_t value;} events[512];
static unsigned event_count;
void nas_input_device_init(struct nas_input_device *d,const char *name) {
    memset(d,0,sizeof(*d));
    size_t n=strlen(name);
    if(n>=sizeof(d->name))n=sizeof(d->name)-1;
    memcpy(d->name,name,n);
    d->evbit=1U;
}
int nas_input_set_capability(struct nas_input_device *d,uint16_t type,uint16_t code){
    uint8_t *bits=NULL;
    if(type==NAS_INPUT_EV_KEY && code<=NAS_INPUT_KEY_MAX)bits=d->keybit;
    if(type==NAS_INPUT_EV_REL && code<=NAS_INPUT_REL_MAX)bits=d->relbit;
    if(type==NAS_INPUT_EV_ABS && code<=NAS_INPUT_ABS_MAX)bits=d->absbit;
    if(type==NAS_INPUT_EV_MSC && code<8)bits=d->mscbit;
    if(type==NAS_INPUT_EV_SW && code<24)bits=d->swbit;
    if(type==NAS_INPUT_EV_LED && code<16)bits=d->ledbit;
    if(type==NAS_INPUT_EV_SND && code<8)bits=d->sndbit;
    if(type==NAS_INPUT_EV_REP && code<8)bits=d->repbit;
    if(!bits)return -EINVAL;
    bits[code/8U] |= (uint8_t)(1U<<(code%8U));
    d->evbit|=1U<<type;return 0;
}
int nas_input_set_property(struct nas_input_device *d,uint16_t code){
    if(code>=sizeof(d->propbit)*8U)return -EINVAL;
    d->propbit[code/8U]|=(uint8_t)(1U<<(code%8U));return 0;
}
int nas_input_set_absinfo(struct nas_input_device *d,uint16_t axis,
                          int32_t min,int32_t max,int32_t fuzz,
                          int32_t flat,int32_t resolution){
    if(axis>NAS_INPUT_ABS_MAX)return -EINVAL;
    d->absinfo[axis]=(struct nas_input_absinfo){0,min,max,fuzz,flat,resolution};
    return nas_input_set_capability(d,NAS_INPUT_EV_ABS,axis);
}
int nas_input_register_device(struct nas_input_device *d){
    assert(d && registered<2 && (d->evbit&~1U));
    received[registered++]=d;d->registered=true;return 0;
}
int nas_input_unregister_device(struct nas_input_device *d){
    if(d->readers)return -EBUSY;
    assert(d->registered);
    d->registered=false;
    for(unsigned i=0;i<2;i++)if(received[i]==d)received[i]=NULL;
    registered--;return 0;
}
void nas_input_event(struct nas_input_device *d,uint16_t type,uint16_t code,
                     int32_t value){
    assert(d->registered && event_count<512);
    events[event_count].type=type;
    events[event_count].code=code;
    events[event_count++].value=value;
}

static void mock_reset(void *p){
    struct fake_input *m=p;m->status=0;m->resets++;
}
static uint32_t mock_get_status(void *p){return ((struct fake_input*)p)->status;}
static void mock_set_status(void *p,uint32_t s){((struct fake_input*)p)->status=s;}
static uint32_t mock_get_features(void *p,unsigned bank){
    return (uint32_t)(((struct fake_input*)p)->features>>(bank*32));
}
static void mock_set_features(void *p,unsigned bank,uint32_t flags){
    (void)p;(void)bank;(void)flags;
}
static int mock_setup(void *p,unsigned qid,unsigned count,
                      uint64_t desc,uint64_t avail,uint64_t used){
    struct fake_input *m=p;
    if(qid>1 || count!=(qid?8U:64U))return -EINVAL;
    m->queues[qid]=(struct fake_vq){count,(struct vring_desc *)(uintptr_t)desc,
        (struct vring_avail *)(uintptr_t)avail,
        (struct vring_used *)(uintptr_t)used};
    return 0;
}
static void mock_notify(void *p,unsigned qid){
    assert(qid<2);((struct fake_input*)p)->notifies++;
}
static uint8_t mock_byte(struct fake_input *m,unsigned off){
    if(off==0)return m->select;
    if(off==1)return m->subsel;
    uint8_t data[128]={0};
    unsigned size=0;
    switch(m->select){
    case 0x01: {
        const char *s=m->tablet?"QEMU Test Tablet":"QEMU Test Keyboard";
        size=strlen(s);memcpy(data,s,size);break;
    }
    case 0x03:
        size=8;data[0]=0x06;data[2]=0x34;data[3]=0x12;
        data[4]=m->tablet?0x02:0x01;data[6]=1;break;
    case 0x10:
        size=1;data[0]=m->tablet?0x02:0;break;
    case 0x11:
        if(m->subsel==1){
            size=m->tablet?35:4;
            if(m->tablet)data[272/8U]|=1U<<(272%8U);
            else data[30/8U]|=1U<<(30%8U);
        }else if(m->subsel==3 && m->tablet){size=1;data[0]=0x03;}
        break;
    case 0x12:
        if(m->tablet && m->subsel<=1){
            size=20;int32_t max=m->subsel?768:1024;
            data[4]=(uint8_t)max;data[5]=(uint8_t)(max>>8);
            data[8]=3;data[12]=4;data[16]=5;
        }
        break;
    }
    if(off==2)return (uint8_t)size;
    return off>=8 && off<8+size ? data[off-8] : 0;
}
static uint32_t mock_get_config32(void *p,uint32_t off){
    uint32_t result=0;
    for(unsigned i=0;i<4;i++)result|=(uint32_t)mock_byte(p,off+i)<<(8*i);
    return result;
}
static int mock_read_config(void *p,uint32_t off,void *out,uint32_t len){
    uint8_t *bytes=out;
    for(uint32_t i=0;i<len;i++)bytes[i]=mock_byte(p,off+i);
    return 0;
}
static int mock_write_config(void *p,uint32_t off,const void *src,uint32_t len){
    struct fake_input *m=p;
    const uint8_t *bytes=src;
    for(uint32_t i=0;i<len;i++) {
        if(off+i==0)m->select=bytes[i];
        else if(off+i==1)m->subsel=bytes[i];
        else return -EINVAL;
    }
    return 0;
}
static const struct nas_virtio_transport_ops mock_ops={
    .reset=mock_reset,.read_status=mock_get_status,
    .write_status=mock_set_status,.read_features=mock_get_features,
    .write_features=mock_set_features,.setup_queue=mock_setup,
    .notify_queue=mock_notify,.read_config32=mock_get_config32,
    .read_config=mock_read_config,.write_config=mock_write_config
};

static void complete(struct fake_input *m,unsigned available,
                     uint16_t type,uint16_t code,int32_t value){
    struct fake_vq *q=&m->queues[0];
    assert(m->status&NAS_VIRTIO_STATUS_DRIVER_OK);
    assert(q->count==64);
    unsigned head=q->avail->ring[available%q->count];
    struct vring_desc *d=&q->desc[head];
    assert(d->flags&VRING_DESC_F_WRITE);
    assert(d->len==8);
    uint8_t *event=(uint8_t *)(uintptr_t)d->addr;
    event[0]=(uint8_t)type;event[1]=(uint8_t)(type>>8);
    event[2]=(uint8_t)code;event[3]=(uint8_t)(code>>8);
    uint32_t bits=(uint32_t)value;
    for(unsigned i=0;i<4;i++)event[4+i]=(uint8_t)(bits>>(i*8));
    unsigned used=q->used->idx;
    q->used->ring[used%q->count]=(struct vring_used_elem){head,8};
    q->used->idx=(uint16_t)(used+1U);
}

int main(void) {
    struct fake_input keyboard={.features=NAS_VIRTIO_F_VERSION_1};
    struct nas_virtio_transport ktr={.context=&keyboard,.ops=&mock_ops,
        .dma=&nas_dma_qemu_coherent};
    struct nas_bus_device kdev={.bus=NAS_BUS_PCI,
        .device_id=18,.resource=&keyboard,.context=&ktr};
    assert(nas_virtio_input_driver_register()==0 && bound_driver);
    assert(bound_driver->match(&kdev));
    assert(bound_driver->probe(&kdev)==0);
    assert(nas_virtio_input_count()==1 && registered==1);
    assert(keyboard.queues[0].avail->idx==64);
    assert(keyboard.queues[1].count==8);
    assert(received[0]->keybit[30/8]&(1U<<(30%8)));
    assert(received[0]->id.vendor==0x1234);
    complete(&keyboard,0,1,30,1);
    complete(&keyboard,1,0,0,0);
    assert(nas_virtio_input_poll(2)==2);
    assert(event_count==2 && events[0].type==1 &&
           events[0].code==30 && events[0].value==1 &&
           events[1].type==0 && events[1].value==0);
    assert(nas_virtio_input_poll(2)==0);
    /* Used ring wraps hundreds of times while buffers are returned. */
    for(unsigned i=0;i<256;i++) {
        complete(&keyboard,64+i,1,30,(int32_t)(i&1U));
        assert(nas_virtio_input_poll(1)==1);
    }
    struct fake_input tablet={.features=NAS_VIRTIO_F_VERSION_1,.tablet=true};
    struct nas_virtio_transport ttr={.context=&tablet,.ops=&mock_ops,
        .dma=&nas_dma_qemu_coherent};
    struct nas_bus_device tdev={.bus=NAS_BUS_VIRTIO_MMIO,
        .device_id=18,.resource=&tablet,.context=&ttr};
    assert(bound_driver->probe(&tdev)==0);
    assert(nas_virtio_input_count()==2 && registered==2);
    assert(received[1]->absbit[0]&3U);
    assert(received[1]->absinfo[0].maximum==1024);
    assert(received[1]->absinfo[1].maximum==768);
    assert(received[1]->absinfo[0].fuzz==3 &&
           received[1]->absinfo[0].flat==4 &&
           received[1]->absinfo[0].resolution==5);
    complete(&tablet,0,3,0,512);
    assert(nas_virtio_input_poll(1)==1);
    assert(events[event_count-1].type==3 &&
           events[event_count-1].value==512);
    received[1]->readers=1;
    assert(bound_driver->remove(&tdev)==-EBUSY);
    received[1]->readers=0;
    assert(bound_driver->remove(&tdev)==0);
    assert(bound_driver->remove(&kdev)==0);
    for(unsigned i=0;i<12;i++)assert(!page_used[i]);
    assert(nas_virtio_input_count()==0 && registered==0);
    printf("[VIRTIO INPUT PORT PASS] Linux config, both vrings, keyboard/ABS, "
           "per-device DMA, %u events, wrap, remove\n",event_count);
    return 0;
}
