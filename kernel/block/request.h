#ifndef NASEED_BLOCK_REQUEST_H
#define NASEED_BLOCK_REQUEST_H

#include "device.h"
#include <stdbool.h>
#include <stdint.h>

/* One kernel-owned request is independent of its submitter's C stack and
 * independent of the hardware DMA mapping. A real driver maps each payload
 * into its own DMA page, and retains the request until its completion has
 * been consumed. This is a bounded, single-core request model, not blk-mq.
 *
 * Neither a user address nor a borrowed filesystem stack buffer survives
 * submission: WRITE copies the source into payload before READY; READ
 * becomes available to its owner only when DONE. The callback cannot run
 * in hardirq and is invoked exactly once from a safe completion context.
 * Driver must reset/stop DMA BEFORE cancelling any published request.
 * A private async-only device descriptor need not be registered in the
 * synchronous BlockDevice registry. Bus owner protects its lifetime with
 * pool.active and may not unbind while a request is owned. */
#define NAS_BLOCK_REQUEST_BUFFER_BYTES 4096U
#define NAS_BLOCK_REQUEST_SLOT_LIMIT 8U

enum nas_block_request_op {
    NAS_BLOCK_REQ_READ = 1,
    NAS_BLOCK_REQ_WRITE = 2,
    NAS_BLOCK_REQ_FLUSH = 3
};
enum nas_block_request_state {
    NAS_BLOCK_REQ_FREE = 0,
    NAS_BLOCK_REQ_READY = 1,
    NAS_BLOCK_REQ_INFLIGHT = 2,
    NAS_BLOCK_REQ_DONE = 3
};
struct nas_block_request_pool;
struct nas_block_request {
    struct nas_block_device *device;
    enum nas_block_request_op op;
    enum nas_block_request_state state;
    uint64_t sector;
    uint32_t sectors, bytes;
    /* The storage driver owns *hardware* DMA and may use distinct pages for
     * each concurrent request. These bytes are kernel-owned, not DMA. */
    uint8_t payload[NAS_BLOCK_REQUEST_BUFFER_BYTES];
    uint64_t deadline_ns;
    int result;
    uint32_t generation;
    void (*done)(struct nas_block_request *, void *);
    void *done_ctx;
    struct nas_block_request_pool *pool;
};
struct nas_block_request_pool {
    struct nas_block_device *device;
    struct nas_block_request slots[NAS_BLOCK_REQUEST_SLOT_LIMIT];
    unsigned active, max_active;
    bool stopping;
};

int nas_block_request_pool_init(struct nas_block_request_pool *,
                                struct nas_block_device *);
/* out is filled only on success; returned request remains pinned in its pool
 * through completion until explicit release. FLUSH requires sectors=0,
 * WRITE requires an input buffer; READ has no incoming bytes. */
int nas_block_request_acquire(struct nas_block_request_pool *,
                              enum nas_block_request_op op,
                              uint64_t sector, uint32_t sectors,
                              const void *write_source, uint64_t deadline_ns,
                              void (*done)(struct nas_block_request *, void *),
                              void *done_ctx,struct nas_block_request **out);
int nas_block_request_mark_submitted(struct nas_block_request *);
/* Completion is terminal and once-only. READ payload must have been copied
 * from a checked DMA response by the driver BEFORE this call. */
int nas_block_request_complete(struct nas_block_request *,int result);
/* A stale-generation completion is rejected, defending against slot reuse. */
int nas_block_request_complete_checked(struct nas_block_request *,
                                       uint32_t generation,int result);
/* Cancel is legal ONLY after the class driver has reset the hardware
 * transport and thereby stopped all DMA to this request's backing. */
int nas_block_request_cancel(struct nas_block_request *,int result);
/* Consumer releases only after terminal completion and any READ copy-out.
 * Releases one pool lifetime pin; existing BlockDevice mount refs are not
 * repurposed as a hidden request reference counter. */
int nas_block_request_release(struct nas_block_request *);
void nas_block_request_pool_begin_remove(struct nas_block_request_pool *);
bool nas_block_request_pool_idle(const struct nas_block_request_pool *);
bool nas_block_request_deadline_expired(const struct nas_block_request *,
                                        uint64_t now_ns);

#endif
