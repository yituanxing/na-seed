/* Real transport device lifecycle with an independent third-party-style
 * callback provider. PCI may implement these same ops without any block
 * request engine change. No fake block filesystems or syscall shims here.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/drivers/virtio/transport.h"
#include "../kernel/drivers/virtio/mmio.h"
#include "../kernel/errno.h"
static struct nas_dma_provider fake_dma={.coherent=true};
struct mock { uint64_t supported, accepted; uint32_t status; unsigned notify;
              unsigned queues; uint64_t desc,avail,used; bool reject; };
static void reset(void *p){((struct mock *)p)->status=0;}
static uint32_t status(void *p){return ((struct mock *)p)->status;}
static void set_status(void *p,uint32_t s){
    struct mock *m=p;
    m->status=m->reject ? (s&~NAS_VIRTIO_STATUS_FEATURES_OK) : s;
}
static uint32_t read_features(void *p,unsigned b){
    return (uint32_t)(((struct mock *)p)->supported>>(b*32U));
}
static void write_features(void *p,unsigned b,uint32_t bits){
    struct mock *m=p;
    if(b==0)m->accepted=(m->accepted&UINT64_C(0xffffffff00000000))|bits;
    else m->accepted=(m->accepted&UINT64_C(0xffffffff))|((uint64_t)bits<<32);
}
static int setup(void *p,unsigned q,unsigned n,uint64_t d,uint64_t a,uint64_t u){
    struct mock *m=p;
    if(q!=0||n!=8)return -EOPNOTSUPP;
    m->queues++;m->desc=d;m->avail=a;m->used=u;return 0;
}
static void notify(void *p,unsigned q){assert(q==0);((struct mock *)p)->notify++;}
static uint32_t config(void *p,uint32_t off){(void)p;return off==0?8192:0;}
static const struct nas_virtio_transport_ops ops={
    .reset=reset,.read_status=status,.write_status=set_status,
    .read_features=read_features,.write_features=write_features,
    .setup_queue=setup,.notify_queue=notify,.read_config32=config
};
int main(void){
    struct mock mock={.supported=NAS_VIRTIO_F_VERSION_1|(UINT64_C(1)<<9)};
    struct nas_virtio_transport t={.context=&mock,.ops=&ops,.dma=&fake_dma};
    uint64_t got=0;
    assert(nas_virtio_negotiate(&t,0,UINT64_C(1)<<9,&got)==0);
    assert(got==mock.supported&&got==mock.accepted);
    assert(nas_virtio_setup_queue(&t,0,8,0x1000,0x2000,0x3000)==0);
    assert(mock.queues==1&&mock.desc==0x1000&&mock.used==0x3000);
    assert(nas_virtio_activate(&t)==0);
    assert(mock.status&NAS_VIRTIO_STATUS_DRIVER_OK);
    nas_virtio_notify(&t,0);assert(mock.notify==1);
    assert(nas_virtio_config32(&t,0)==8192);
    mock.supported=UINT64_C(1)<<9;
    assert(nas_virtio_negotiate(&t,0,UINT64_C(1)<<9,&got)==-EOPNOTSUPP);
    assert(mock.status&NAS_VIRTIO_STATUS_FAILED);
    mock.supported=NAS_VIRTIO_F_VERSION_1;
    mock.reject=true;
    assert(nas_virtio_negotiate(&t,0,UINT64_C(1)<<9,&got)==-EOPNOTSUPP);
    assert(mock.status&NAS_VIRTIO_STATUS_FAILED);
    mock.reject=false;
    assert(nas_virtio_negotiate(&t,0,0,&got)==0);
    assert(nas_virtio_setup_queue(&t,7,8,0x1000,0x2000,0x3000)==-EOPNOTSUPP);
    assert(mock.status&NAS_VIRTIO_STATUS_FAILED);
    volatile uint32_t mmio[128]={0};
    mmio[0]=0x74726976U;mmio[1]=2;mmio[2]=2;
    struct nas_virtio_transport mmio_transport={0};
    assert(nas_virtio_mmio_transport_init(&mmio_transport,mmio,&fake_dma)==0);
    assert(nas_virtio_config32(&mmio_transport,0)==0);
    mmio[0x100/4]=1024;
    assert(nas_virtio_config32(&mmio_transport,0)==1024);
    assert(nas_virtio_setup_queue(&mmio_transport,0,8,
                                 0x100000,0x101000,0x102000)==-EOPNOTSUPP);
    mmio[0x034/4]=8;
    assert(nas_virtio_setup_queue(&mmio_transport,0,8,
                                  0x100000,0x101000,0x102000)==0);
    assert(mmio[0x044/4]==1&&mmio[0x080/4]==0x100000);
    puts("[VIRTIO TRANSPORT PASS] same block-facing ops on mock and modern MMIO, fail closed");
    return 0;
}
