#include "mmio.h"
#include "../../errno.h"
#include <stddef.h>
#include <stdint.h>
#define R_MAGIC        0x000U
#define R_VERSION      0x004U
#define R_DEVICE_ID    0x008U
#define R_FEATURES     0x010U
#define R_FEATURE_SEL  0x014U
#define R_DRIVER_FEAT  0x020U
#define R_DRIVER_SEL   0x024U
#define R_QUEUE_SEL    0x030U
#define R_QUEUE_MAX    0x034U
#define R_QUEUE_NUM    0x038U
#define R_QUEUE_READY  0x044U
#define R_QUEUE_NOTIFY 0x050U
#define R_STATUS       0x070U
#define R_DESC_LO      0x080U
#define R_DESC_HI      0x084U
#define R_DRIVER_LO    0x090U
#define R_DRIVER_HI    0x094U
#define R_DEVICE_LO    0x0a0U
#define R_DEVICE_HI    0x0a4U
#define R_DEVICE_CFG   0x100U
#define MAGIC          0x74726976U
#define VERSION        2U
static volatile uint32_t *regs(void *ctx) {return (volatile uint32_t *)ctx;}
static uint32_t rd(void *c,unsigned off){return regs(c)[off/4U];}
static void wr(void *c,unsigned off,uint32_t v){regs(c)[off/4U]=v;}
static void reset(void *c){wr(c,R_STATUS,0);}
static uint32_t read_status(void *c){return rd(c,R_STATUS);}
static void write_status(void *c,uint32_t v){wr(c,R_STATUS,v);}
static uint32_t read_features(void *c,unsigned bank){
    wr(c,R_FEATURE_SEL,bank);
    return rd(c,R_FEATURES);
}
static void write_features(void *c,unsigned bank,uint32_t bits){
    wr(c,R_DRIVER_SEL,bank);
    wr(c,R_DRIVER_FEAT,bits);
}
static int setup_queue(void *c,unsigned qid,unsigned count,uint64_t desc,
                       uint64_t avail,uint64_t used){
    wr(c,R_QUEUE_SEL,qid);
    if(rd(c,R_QUEUE_READY)||rd(c,R_QUEUE_MAX)<count)return -EOPNOTSUPP;
    wr(c,R_QUEUE_NUM,count);
    wr(c,R_DESC_LO,(uint32_t)desc);
    wr(c,R_DESC_HI,(uint32_t)(desc>>32));
    wr(c,R_DRIVER_LO,(uint32_t)avail);
    wr(c,R_DRIVER_HI,(uint32_t)(avail>>32));
    wr(c,R_DEVICE_LO,(uint32_t)used);
    wr(c,R_DEVICE_HI,(uint32_t)(used>>32));
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    wr(c,R_QUEUE_READY,1);
    return 0;
}
static void notify(void *c,unsigned qid){wr(c,R_QUEUE_NOTIFY,qid);}
static uint32_t read_cfg32(void *c,uint32_t off){
    return (off&3U)?0:rd(c,R_DEVICE_CFG+off);
}
static const struct nas_virtio_transport_ops ops={
    .reset=reset,.read_status=read_status,.write_status=write_status,
    .read_features=read_features,.write_features=write_features,
    .setup_queue=setup_queue,.notify_queue=notify,.read_config32=read_cfg32
};
int nas_virtio_mmio_transport_init(struct nas_virtio_transport *out,
                                  volatile uint32_t *mmio,
                                  const struct nas_dma_provider *dma) {
    if(!out||!mmio||!dma)return -EINVAL;
    if(mmio[R_MAGIC/4]!=MAGIC || mmio[R_VERSION/4]!=VERSION ||
       !mmio[R_DEVICE_ID/4])return -ENODEV;
    *out=(struct nas_virtio_transport){.context=(void *)mmio,
                                      .ops=&ops,.dma=dma};
    return 0;
}
