/* Real common BlockDevice/RamDisk implementations, compiled on the host.
 * No fake disk request or fake VFS syscall is accepted as a passing test. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/block/device.h"
#include "../kernel/block/ramdisk.h"
#include "../kernel/errno.h"

static uint8_t raw[8192], copied[8192];
static struct nas_ramdisk rd, second;
static unsigned reads, writes, flushes, max_observed;
static int injected_error;
static int mock_read(struct nas_block_device *dev, uint64_t sector,
                     uint32_t count, void *buf) {
    (void)dev;
    reads++;
    if(count > max_observed) max_observed=count;
    if(injected_error) return -EIO;
    memset(buf, (int)(sector&255U), (size_t)count*512);
    return 0;
}
static int mock_write(struct nas_block_device *dev, uint64_t sector,
                      uint32_t count, const void *buf) {
    (void)dev; (void)sector; (void)count; (void)buf;
    writes++; return 0;
}
static int mock_flush(struct nas_block_device *dev) {
    (void)dev; flushes++; return 0;
}
static const struct nas_block_ops mock_ops = {
    .read=mock_read,.write=mock_write,.flush=mock_flush
};
static struct nas_block_device mock = {
    .name="mock",.sectors=2048,.sector_bytes=512,
    .max_sectors_per_io=3,.ops=&mock_ops
};
int main(void) {
    for (size_t i=0;i<sizeof(raw);i++) raw[i]=(uint8_t)(i*17U+7U);
    assert(nas_ramdisk_attach(&rd,"disk0",raw,sizeof(raw),512,true)==0);
    assert(nas_block_get("missing")==NULL);
    struct nas_block_device *pinned=nas_block_get("disk0");
    assert(pinned==&rd.block && pinned->refs==1);
    assert(nas_ramdisk_detach(&rd)==-EBUSY);
    assert(nas_block_read(pinned,2,3,copied)==0);
    assert(!memcmp(copied,raw+1024,1536));
    assert(nas_block_read(pinned,UINT64_MAX,1,copied)==-EINVAL);
    assert(nas_block_read(pinned,16,1,copied)==-EINVAL);
    assert(nas_block_read(pinned,15,2,copied)==-EINVAL);
    assert(nas_block_write(pinned,15,2,copied)==-EINVAL);
    assert(nas_block_read(pinned,3,1,NULL)==-EINVAL);
    assert(nas_block_read(pinned,3,0,NULL)==0);
    memset(copied,0xab,1024);
    assert(nas_block_write(pinned,4,2,copied)==0);
    assert(!memcmp(raw+2048,copied,1024));
    assert(nas_block_flush(pinned)==0);
    assert(nas_ramdisk_attach(&second,"disk0",copied,1024,512,true)==-EEXIST);
    assert(!second.block.registered);
    nas_block_put(pinned);
    assert(nas_ramdisk_detach(&rd)==0);
    assert(nas_block_read(pinned,0,1,copied)==-ENODEV);
    assert(nas_ramdisk_attach(&rd,"disk0",raw,sizeof(raw),512,false)==0);
    assert(nas_block_write(&rd.block,0,1,copied)==-EROFS);
    assert(nas_block_flush(&rd.block)==0);
    assert(nas_ramdisk_detach(&rd)==0);
    assert(nas_block_register(&mock)==0);
    memset(copied,0,4096);
    assert(nas_block_read(&mock,7,8,copied)==0);
    assert(reads==3 && max_observed==3);
    assert(copied[0]==7 && copied[1536]==10 && copied[3072]==13);
    assert(nas_block_write(&mock,1,7,copied)==0 && writes==3);
    assert(nas_block_flush(&mock)==0 && flushes==1);
    injected_error=1;
    assert(nas_block_read(&mock,0,1,copied)==-EIO);
    injected_error=0;
    assert(nas_block_unregister(&mock)==0);
    assert(nas_block_flush(&mock)==-ENODEV);
    struct nas_block_device invalid = mock;
    invalid.sector_bytes=1000;
    assert(nas_block_register(&invalid)==-EINVAL);
    invalid=mock;
    invalid.ops=&(const struct nas_block_ops){.read=mock_read,.write=mock_write};
    assert(nas_block_register(&invalid)==0);
    assert(nas_block_flush(&invalid)==-EOPNOTSUPP);
    assert(nas_block_unregister(&invalid)==0);
    puts("[BLOCK FOUNDATION PASS] registration, pins, RAM ext4-image sector I/O, range, RO, chunking, error and flush");
    return 0;
}
