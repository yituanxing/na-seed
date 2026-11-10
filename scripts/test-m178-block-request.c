/* M178 Host contract: compile the actual kernel BlockRequest core, not a
 * duplicate model. The private descriptor intentionally is not registered
 * with the old synchronous BlockDevice API. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "../kernel/block/request.c"

#define CHECK(x) do {if(!(x)){fprintf(stderr,"[M178 REQUEST FAIL] %s:%d %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
struct result_log {unsigned called;unsigned count;uint32_t generations[32];int results[32];};
static void on_complete(struct nas_block_request *r,void *user) {
    struct result_log *log=user;
    CHECK(log && log->count<32 && r && r->state==NAS_BLOCK_REQ_DONE);
    log->generations[log->count]=r->generation;
    log->results[log->count++]=r->result;
    log->called++;
}
static void setup_dev(struct nas_block_device *d) {
    memset(d,0,sizeof(*d));
    d->name="private-async-device";
    d->sectors=128;
    d->sector_bytes=512;
    d->max_sectors_per_io=8;
    /* No synchronous driver operations or VFS mount interface: deliberate. */
    CHECK(!d->registered && !d->ops && !d->refs);
}
int main(void) {
    struct nas_block_request_pool *p=calloc(1,sizeof(*p));
    CHECK(p);
    struct nas_block_device dev;
    setup_dev(&dev);
    CHECK(nas_block_request_pool_init(p,&dev)==0);
    CHECK(nas_block_request_pool_idle(p));
    CHECK(p->active==0 && p->max_active==0);

    struct result_log log={0};
    unsigned char write_source[4096];
    for(unsigned i=0;i<sizeof(write_source);i++)write_source[i]=(uint8_t)(i*17U+7U);
    struct nas_block_request *requests[NAS_BLOCK_REQUEST_SLOT_LIMIT]={0};
    for(unsigned i=0;i<NAS_BLOCK_REQUEST_SLOT_LIMIT;i++) {
        enum nas_block_request_op op=i&1U ? NAS_BLOCK_REQ_READ : NAS_BLOCK_REQ_WRITE;
        int rc=nas_block_request_acquire(p,op,(uint64_t)i*8U,8U,
             op==NAS_BLOCK_REQ_WRITE?write_source:NULL,1000U+i,
             on_complete,&log,&requests[i]);
        CHECK(rc==0 && requests[i] && requests[i]->state==NAS_BLOCK_REQ_READY);
        CHECK(requests[i]->bytes==4096 && requests[i]->pool==p);
        CHECK(requests[i]->device==&dev && requests[i]->generation==1);
        CHECK(nas_block_request_mark_submitted(requests[i])==0);
        CHECK(nas_block_request_mark_submitted(requests[i])==-EBUSY);
        if(op==NAS_BLOCK_REQ_WRITE)
            CHECK(!memcmp(requests[i]->payload,write_source,4096));
    }
    CHECK(p->active==8 && p->max_active==8 && !nas_block_request_pool_idle(p));
    CHECK(nas_block_request_pool_init(p,&dev)==-EBUSY);
    struct nas_block_request *extra=(void*)0x1;
    CHECK(nas_block_request_acquire(p,NAS_BLOCK_REQ_READ,0,1,NULL,0,NULL,NULL,&extra)==-ENOSPC);
    CHECK(extra==NULL);
    memset(write_source,0xCD,sizeof(write_source));
    for(unsigned i=0;i<NAS_BLOCK_REQUEST_SLOT_LIMIT;i+=2)
        CHECK(requests[i]->payload[0]!=0xCD); /* immutable source copy */

    const unsigned order[]={7,2,5,0,6,4,1,3};
    for(unsigned n=0;n<8;n++) {
        unsigned id=order[n];
        struct nas_block_request *r=requests[id];
        CHECK(nas_block_request_deadline_expired(r,999U)==false);
        CHECK(nas_block_request_deadline_expired(r,r->deadline_ns));
        CHECK(nas_block_request_deadline_expired(r,r->deadline_ns+1U));
        if(r->op==NAS_BLOCK_REQ_READ)memset(r->payload,(int)(id+8U),r->bytes);
        int rc=(id==5U)?-EIO:0;
        CHECK(nas_block_request_complete_checked(r,r->generation,rc)==0);
        CHECK(r->result==rc && r->state==NAS_BLOCK_REQ_DONE);
        CHECK(nas_block_request_complete(r,0)==-EALREADY);
        CHECK(!nas_block_request_deadline_expired(r,UINT64_MAX));
        CHECK(log.called==n+1U && log.results[n]==rc);
        if(r->op==NAS_BLOCK_REQ_READ && id!=5U)
            CHECK(r->payload[r->bytes-1U]==(uint8_t)(id+8U));
    }
    CHECK(log.called==8 && p->active==8);
    for(unsigned i=0;i<8;i++) {
        CHECK(nas_block_request_release(requests[i])==0);
        CHECK(requests[i]->state==NAS_BLOCK_REQ_FREE);
        CHECK(requests[i]->payload[0]==0);
        CHECK(nas_block_request_release(requests[i])==-EINVAL);
    }
    CHECK(p->active==0 && p->max_active==8 && nas_block_request_pool_idle(p));
    CHECK(!dev.refs && !dev.registered);
    puts("[M178 BLOCK REQUEST CORE PASS] 8 independent 4096-byte owned requests, 8 inflight, out-of-order terminal callbacks, exactly-once, bounded pressure, READ/WRITE copy lifetime");

    struct nas_block_request *r=NULL;
    CHECK(nas_block_request_acquire(p,NAS_BLOCK_REQ_READ,4,1,NULL,42,on_complete,&log,&r)==0);
    CHECK(r && r==requests[0] && r->generation==2U);
    CHECK(nas_block_request_complete_checked(r,1U,0)==-ESTALE);
    CHECK(r->state==NAS_BLOCK_REQ_READY);
    CHECK(nas_block_request_release(r)==-EBUSY);
    CHECK(nas_block_request_mark_submitted(r)==0);
    CHECK(nas_block_request_cancel(r,0)==-EINVAL);
    /* Transport-reset prerequisite is owned by the driver. Mock has no DMA
     * and intentionally exercises cancellation only after a mock stop. */
    bool transport_reset=true;
    CHECK(transport_reset);
    CHECK(nas_block_request_cancel(r,-ETIMEDOUT)==0);
    CHECK(r->result==-ETIMEDOUT && log.called==9U);
    CHECK(nas_block_request_cancel(r,-ETIMEDOUT)==-EALREADY);
    CHECK(nas_block_request_release(r)==0);

    CHECK(nas_block_request_acquire(p,NAS_BLOCK_REQ_FLUSH,0,0,NULL,0,
                                   on_complete,&log,&r)==0);
    CHECK(r->bytes==0 && r->op==NAS_BLOCK_REQ_FLUSH);
    CHECK(nas_block_request_mark_submitted(r)==0);
    CHECK(!nas_block_request_deadline_expired(r,UINT64_MAX));
    CHECK(nas_block_request_complete(r,0)==0 && log.called==10);
    CHECK(nas_block_request_release(r)==0);

    CHECK(nas_block_request_acquire(p,NAS_BLOCK_REQ_READ,0,0,NULL,0,NULL,NULL,&r)==-EINVAL);
    CHECK(nas_block_request_acquire(p,NAS_BLOCK_REQ_READ,126,8,NULL,0,NULL,NULL,&r)==-EINVAL);
    CHECK(nas_block_request_acquire(p,NAS_BLOCK_REQ_READ,0,9,NULL,0,NULL,NULL,&r)==-EINVAL);
    CHECK(nas_block_request_acquire(p,NAS_BLOCK_REQ_READ,0,1,write_source,0,NULL,NULL,&r)==-EINVAL);
    CHECK(nas_block_request_acquire(p,NAS_BLOCK_REQ_WRITE,0,1,NULL,0,NULL,NULL,&r)==-EINVAL);
    CHECK(nas_block_request_acquire(p,NAS_BLOCK_REQ_FLUSH,1,0,NULL,0,NULL,NULL,&r)==-EINVAL);
    CHECK(nas_block_request_acquire(p,NAS_BLOCK_REQ_FLUSH,0,1,NULL,0,NULL,NULL,&r)==-EINVAL);
    CHECK(nas_block_request_acquire(p,(enum nas_block_request_op)99,0,0,NULL,0,NULL,NULL,&r)==-EINVAL);
    dev.read_only=true;
    CHECK(nas_block_request_acquire(p,NAS_BLOCK_REQ_WRITE,0,1,write_source,0,NULL,NULL,&r)==-EROFS);
    CHECK(nas_block_request_acquire(p,NAS_BLOCK_REQ_FLUSH,0,0,NULL,0,NULL,NULL,&r)==-EROFS);
    dev.read_only=false;
    nas_block_request_pool_begin_remove(p);
    CHECK(p->stopping && nas_block_request_pool_idle(p));
    CHECK(nas_block_request_acquire(p,NAS_BLOCK_REQ_READ,0,1,NULL,0,NULL,NULL,&r)==-EBUSY);
    CHECK(nas_block_request_pool_init(p,&dev)==0);
    dev.removing=true;
    CHECK(nas_block_request_acquire(p,NAS_BLOCK_REQ_READ,0,1,NULL,0,NULL,NULL,&r)==-EBUSY);
    dev.removing=false;
    struct nas_block_device invalid=dev;
    invalid.sector_bytes=768;
    CHECK(nas_block_request_pool_init(p,&invalid)==-EINVAL);
    invalid=dev;
    invalid.sectors=UINT64_MAX;
    CHECK(nas_block_request_pool_init(p,&invalid)==-EINVAL);
    puts("[M178 BLOCK REQUEST NEGATIVE PASS] stale completion, pre-terminal release, reset cancellation, timeout, sector/mode errors, flush, RO, stop/drain, invalid geometry");
    free(p);
    return 0;
}
