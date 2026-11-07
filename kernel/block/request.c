#include "request.h"
#include "../errno.h"
#include "../lib/string.h"
#include <stddef.h>
#include <stdint.h>

/* Kernel C and the safe scheduler drain run with local device IRQs masked.
 * No spinlock, worker scheduling or device polling belongs in this generic
 * request core. Hardware transport owns submission and IRQ notification. */
static bool sector_size_valid(uint32_t n) {
    return n>=512U && n<=4096U && !(n&(n-1U));
}
static bool request_in_pool(const struct nas_block_request *r) {
    if(!r || !r->pool)return false;
    const struct nas_block_request_pool *p=r->pool;
    for(unsigned i=0;i<NAS_BLOCK_REQUEST_SLOT_LIMIT;i++)
        if(r==&p->slots[i])return true;
    return false;
}
int nas_block_request_pool_init(struct nas_block_request_pool *p,
                                struct nas_block_device *dev) {
    if(!p || !dev || !dev->sectors || !sector_size_valid(dev->sector_bytes) ||
       !dev->max_sectors_per_io ||
       dev->sectors>UINT64_MAX/dev->sector_bytes || dev->removing)
        return -EINVAL;
    if(p->device && p->active)return -EBUSY;
    memset(p,0,sizeof(*p));
    p->device=dev;
    return 0;
}
int nas_block_request_acquire(struct nas_block_request_pool *p,
                              enum nas_block_request_op op,
                              uint64_t sector, uint32_t sectors,
                              const void *write_source, uint64_t deadline_ns,
                              void (*done)(struct nas_block_request *,void *),
                              void *done_ctx,struct nas_block_request **out) {
    if(!p || !p->device || !out)return -EINVAL;
    *out=NULL;
    struct nas_block_device *dev=p->device;
    if(p->stopping || dev->removing)return -EBUSY;
    if(op==NAS_BLOCK_REQ_FLUSH) {
        if(sector || sectors || write_source)return -EINVAL;
        if(dev->read_only)return -EROFS;
    }else if(op==NAS_BLOCK_REQ_READ || op==NAS_BLOCK_REQ_WRITE) {
        if(!sectors || sector>=dev->sectors ||
           sectors>dev->sectors-sector ||
           sectors>dev->max_sectors_per_io ||
           (uint64_t)sectors*dev->sector_bytes>NAS_BLOCK_REQUEST_BUFFER_BYTES)
            return -EINVAL;
        if(op==NAS_BLOCK_REQ_WRITE && (!write_source || dev->read_only))
            return !write_source ? -EINVAL : -EROFS;
        if(op==NAS_BLOCK_REQ_READ && write_source)return -EINVAL;
    }else return -EINVAL;
    struct nas_block_request *r=NULL;
    for(unsigned i=0;i<NAS_BLOCK_REQUEST_SLOT_LIMIT;i++) {
        if(p->slots[i].state==NAS_BLOCK_REQ_FREE) {
            r=&p->slots[i];break;
        }
    }
    if(!r)return -ENOSPC;
    uint32_t next=r->generation+1U;
    if(!next)return -EOVERFLOW;
    memset(r,0,sizeof(*r));
    r->pool=p;
    r->device=dev;
    r->op=op;
    r->sector=sector;
    r->sectors=sectors;
    r->bytes=op==NAS_BLOCK_REQ_FLUSH ? 0 : sectors*dev->sector_bytes;
    r->deadline_ns=deadline_ns;
    r->done=done;
    r->done_ctx=done_ctx;
    r->generation=next;
    if(op==NAS_BLOCK_REQ_WRITE)memcpy(r->payload,write_source,r->bytes);
    r->state=NAS_BLOCK_REQ_READY;
    ++p->active;
    if(p->active>p->max_active)p->max_active=p->active;
    *out=r;
    return 0;
}
int nas_block_request_mark_submitted(struct nas_block_request *r) {
    if(!request_in_pool(r))return -EINVAL;
    if(r->state!=NAS_BLOCK_REQ_READY)return -EBUSY;
    r->state=NAS_BLOCK_REQ_INFLIGHT;
    return 0;
}
int nas_block_request_complete(struct nas_block_request *r,int result) {
    if(!request_in_pool(r))return -EINVAL;
    if(r->state==NAS_BLOCK_REQ_FREE)return -ENODEV;
    if(r->state==NAS_BLOCK_REQ_DONE)return -EALREADY;
    if(r->state!=NAS_BLOCK_REQ_READY && r->state!=NAS_BLOCK_REQ_INFLIGHT)
        return -EINVAL;
    /* Do not turn an invalid device status into successful completion. */
    r->result=result<=0 ? result:-EIO;
    r->state=NAS_BLOCK_REQ_DONE;
    if(r->done)r->done(r,r->done_ctx);
    return 0;
}
int nas_block_request_complete_checked(struct nas_block_request *r,
                                       uint32_t generation,int result) {
    if(!request_in_pool(r))return -EINVAL;
    if(!generation || r->generation!=generation)return -ESTALE;
    return nas_block_request_complete(r,result);
}
int nas_block_request_cancel(struct nas_block_request *r,int result) {
    if(result>=0)return -EINVAL;
    /* Only legal after the device has stopped DMA. Drivers must enforce
     * reset ordering; this generic layer cannot observe the transport. */
    return nas_block_request_complete(r,result);
}
int nas_block_request_release(struct nas_block_request *r) {
    if(!request_in_pool(r))return -EINVAL;
    if(r->state!=NAS_BLOCK_REQ_DONE)return -EBUSY;
    struct nas_block_request_pool *p=r->pool;
    if(!p->active)return -EIO;
    const uint32_t generation=r->generation;
    /* Clear any completed READ result before recycling the fixed slot. */
    memset(r,0,sizeof(*r));
    r->generation=generation;
    --p->active;
    return 0;
}
void nas_block_request_pool_begin_remove(struct nas_block_request_pool *p) {
    if(p && p->device)p->stopping=true;
}
bool nas_block_request_pool_idle(const struct nas_block_request_pool *p) {
    return p && p->device && p->active==0;
}
bool nas_block_request_deadline_expired(const struct nas_block_request *r,
                                        uint64_t now_ns) {
    return request_in_pool(r) && r->deadline_ns!=0 &&
           (r->state==NAS_BLOCK_REQ_READY ||
            r->state==NAS_BLOCK_REQ_INFLIGHT) &&
           now_ns>=r->deadline_ns;
}
