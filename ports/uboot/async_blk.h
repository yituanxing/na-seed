#ifndef NASEED_ASYNC_VIRTIO_BLK_H
#define NASEED_ASYNC_VIRTIO_BLK_H

#include "../../kernel/drivers/core/bus.h"
#include "../../kernel/block/request.h"
#include <stdbool.h>
#include <stdint.h>

/* M178: a second, dedicated 16MiB test disk. The original U-Boot driver
 * retains exclusive ownership of the boot disk and its synchronous path.
 * This driver's BlockRequest objects outlive their submitting C frames;
 * completion is never performed in the hardware interrupt handler. */
#define NAS_ASYNC_BLK_FIXTURE_SECTORS UINT64_C(32768)

struct nas_async_blk_stats {
    uint64_t submitted, completed, irq_ack, deferred, fallback_poll;
    uint64_t verified_reads, verified_writes, flushes;
    uint64_t worker_waits; /* M179: actual kernel-worker IRQ sleeps */
    unsigned max_inflight;
};

int nas_async_blk_driver_register(void);
unsigned nas_async_blk_count(void);
bool nas_async_blk_is_fixture(const struct nas_bus_device *);
bool nas_async_blk_owns_device(const struct nas_bus_device *);
int nas_async_blk_submit(struct nas_block_request *);
void nas_async_blk_service(void);
void nas_async_blk_native_test_step(void);
void nas_async_blk_get_stats(struct nas_async_blk_stats *);

#ifdef NAS_ENABLE_M179_FS_IRQ
/* The M178 raw-sector oracle runs first. Afterwards, a kernel worker may
 * publish the same physical device via a sleepable BlockDevice adapter and
 * a validated MBR partition. lwext4 still sees its upstream synchronous
 * block callbacks; the worker parks until genuine IRQ/deferred completion.
 * The boot disk retains its independent original U-Boot polling port. */
bool nas_async_blk_fixture_ready(void);
int nas_async_blk_register_block_device(void);
const char *nas_async_blk_mountable_partition(void);
#endif

#endif
