/* Real adapter + BlockDevice regression: two foreign callback ABIs and
 * identical VFS-facing BlockDevice I/O, not a fake parallel registry. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/drivers/port/block.h"
#include "../kernel/block/partition.h"
#include "../kernel/errno.h"

static uint8_t disk[8192], buffer[4096];
struct backend { int mode; int fail; int short_io; unsigned reads, writes, flushes; };
static int64_t source_read(void *ctx, uint64_t lba, uint32_t count, void *dst) {
    struct backend *b=ctx;
    ++b->reads;
    if(b->fail)return -EIO;
    memcpy(dst,disk+(size_t)lba*512,(size_t)count*512);
    if(b->short_io)return b->mode ? (int64_t)count-1 : 1;
    return b->mode ? (int64_t)count : 0;
}
static int64_t source_write(void *ctx, uint64_t lba,
                            uint32_t count, const void *src) {
    struct backend *b=ctx;
    ++b->writes;
    if(b->fail)return -EIO;
    memcpy(disk+(size_t)lba*512,src,(size_t)count*512);
    if(b->short_io)return b->mode ? (int64_t)count-1 : 1;
    return b->mode ? (int64_t)count : 0;
}
static int source_flush(void *ctx) {
    struct backend *b=ctx;
    ++b->flushes;
    return b->fail ? -EIO : 0;
}
static const struct nas_port_block_ops upstream={
    .read=source_read,.write=source_write,.flush=source_flush
};
static struct nas_port_block port;
int main(void) {
    for(size_t i=0;i<sizeof(disk);i++)disk[i]=(uint8_t)(i*11U);
    struct backend b={.mode=1};
    assert(nas_port_block_attach(&port,"ported",&b,16,512,2,false,
                                 NAS_PORT_BLOCK_SECTORS,&upstream)==0);
    struct nas_block_device *dev=nas_block_get("ported");
    assert(dev==&port.block);
    assert(nas_port_block_detach(&port)==-EBUSY);
    assert(nas_block_read(dev,1,5,buffer)==0);
    assert(b.reads==3 && !memcmp(buffer,disk+512,2560));
    memset(buffer,0xc7,2048);
    assert(nas_block_write(dev,3,4,buffer)==0);
    assert(b.writes==2 && !memcmp(disk+1536,buffer,2048));
    assert(nas_block_flush(dev)==0 && b.flushes==1);
    /* The independent upstream driver adapter shares the same partition
     * ownership/offset semantics, not a second registry or FS shim. */
    struct nas_partition_view view={0};
    struct nas_partition_extent extent={.first_lba=2,.sectors=12};
    assert(nas_partition_attach(&view,"ported-part","ported",&extent,false)==0);
    struct nas_block_device *part=nas_block_get("ported-part");
    assert(part==&view.block);
    assert(nas_block_read(part,1,2,buffer)==0);
    assert(!memcmp(buffer,disk+3*512,1024));
    assert(nas_block_flush(part)==0 && b.flushes==2);
    assert(nas_partition_detach(&view)==-EBUSY);
    nas_block_put(part);
    assert(nas_partition_detach(&view)==0);
    b.short_io=1;
    assert(nas_block_read(dev,1,2,buffer)==-EIO);
    assert(nas_block_write(dev,1,2,buffer)==-EIO);
    b.short_io=0;
    b.fail=1;
    assert(nas_block_read(dev,1,1,buffer)==-EIO);
    assert(nas_block_flush(dev)==-EIO);
    b.fail=0;
    nas_block_put(dev);
    assert(nas_port_block_detach(&port)==0);
    assert(nas_block_get("ported")==NULL);
    b.mode=0;b.reads=0;b.writes=0;
    assert(nas_port_block_attach(&port,"ported",&b,16,512,3,false,
                                 NAS_PORT_BLOCK_ZERO,&upstream)==0);
    dev=nas_block_get("ported");
    assert(nas_block_read(dev,1,4,buffer)==0 && b.reads==2);
    b.short_io=1;
    assert(nas_block_read(dev,1,1,buffer)==-EIO);
    b.short_io=0;
    nas_block_put(dev);
    assert(nas_port_block_detach(&port)==0);
    const struct nas_port_block_ops noflush={
        .read=source_read,.write=source_write
    };
    assert(nas_port_block_attach(&port,"readonly",&b,16,512,1,false,
                                 NAS_PORT_BLOCK_ZERO,&noflush)==-EINVAL);
    assert(nas_port_block_attach(&port,"readonly",&b,16,512,1,true,
                                 NAS_PORT_BLOCK_ZERO,&noflush)==0);
    dev=nas_block_get("readonly");
    assert(nas_block_write(dev,0,1,buffer)==-EROFS);
    assert(nas_block_flush(dev)==0);
    nas_block_put(dev);
    assert(nas_port_block_detach(&port)==0);
    puts("[DRIVER PORT PASS] third-party count/zero I/O, short/error/flush, mount pins, RO");
    return 0;
}
