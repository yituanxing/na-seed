#include "block_adapter.h"
#include "../../kernel/errno.h"
#include "../../kernel/lib/string.h"
#include <stdint.h>
#include <stddef.h>
#define SECTOR 512U
#define LFS_ERASE 4096U
#define LFS_PROGRAM 256U
/* BlockDevice is sector-based; littlefs uses erase/program/read granularity.
 * The adapter is the ONLY conversion boundary, shared by every ISA. */
static int io_bounds(const struct lfs_config *cfg,lfs_block_t block,
                     lfs_off_t off,lfs_size_t size) {
    if(!cfg || !cfg->context || block>=cfg->block_count ||
       off>cfg->block_size || size>cfg->block_size-off)return LFS_ERR_INVAL;
    return 0;
}
static int disk_read(const struct lfs_config *cfg,lfs_block_t block,
                     lfs_off_t off,void *buffer,lfs_size_t size) {
    if(io_bounds(cfg,block,off,size))return LFS_ERR_INVAL;
    if(!buffer && size)return LFS_ERR_INVAL;
    struct nas_littlefs_block *p=cfg->context;
    uint8_t sector[SECTOR];
    uint8_t *dst=buffer;
    while(size) {
        uint64_t pos=(uint64_t)block*cfg->block_size+off;
        uint64_t index=pos/SECTOR;
        unsigned within=(unsigned)(pos%SECTOR);
        unsigned count=SECTOR-within;
        if(count>size)count=size;
        if(nas_block_read(p->device,index,1,sector))return LFS_ERR_IO;
        memcpy(dst,sector+within,count);
        off+=count;dst+=count;size-=count;
    }
    return 0;
}
static int disk_prog(const struct lfs_config *cfg,lfs_block_t block,
                     lfs_off_t off,const void *buffer,lfs_size_t size) {
    if(io_bounds(cfg,block,off,size) || (off%cfg->prog_size) ||
       (size%cfg->prog_size) || (!buffer && size))return LFS_ERR_INVAL;
    struct nas_littlefs_block *p=cfg->context;
    uint8_t sector[SECTOR];
    const uint8_t *src=buffer;
    while(size) {
        uint64_t pos=(uint64_t)block*cfg->block_size+off;
        uint64_t index=pos/SECTOR;
        unsigned within=(unsigned)(pos%SECTOR);
        unsigned count=SECTOR-within;
        if(count>size)count=size;
        if(nas_block_read(p->device,index,1,sector))return LFS_ERR_IO;
        /* Real flash can only program 1 -> 0 without erase; enforce this
         * in the RAM-disk host oracle, rather than silently lying. */
        for(unsigned i=0;i<count;i++)
            if((sector[within+i]&src[i])!=src[i])return LFS_ERR_CORRUPT;
        for(unsigned i=0;i<count;i++)sector[within+i]&=src[i];
        if(nas_block_write(p->device,index,1,sector))return LFS_ERR_IO;
        src+=count;off+=count;size-=count;
    }
    return 0;
}
static int disk_erase(const struct lfs_config *cfg,lfs_block_t block) {
    if(!cfg || block>=cfg->block_count)return LFS_ERR_INVAL;
    struct nas_littlefs_block *p=cfg->context;
    uint8_t page[SECTOR];
    memset(page,0xff,sizeof(page));
    for(unsigned i=0;i<cfg->block_size/SECTOR;i++)
        if(nas_block_write(p->device,
            (uint64_t)block*(cfg->block_size/SECTOR)+i,1,page))
            return LFS_ERR_IO;
    return 0;
}
static int disk_sync(const struct lfs_config *cfg) {
    struct nas_littlefs_block *p=cfg->context;
    return nas_block_flush(p->device)?LFS_ERR_IO:0;
}
int nas_littlefs_block_attach(struct nas_littlefs_block *p,
                               const char *block_name) {
    if(!p || !block_name || p->device)return -EINVAL;
    struct nas_block_device *dev=nas_block_get(block_name);
    if(!dev)return -ENODEV;
    /* No fake durability on a no-flush backend. Limit to 512-byte sector
     * devices for explicit, audited erase/read-modify-program semantics. */
    if(dev->sector_bytes!=SECTOR || dev->read_only ||
       !dev->ops || !dev->ops->flush ||
       (dev->sectors%(LFS_ERASE/SECTOR)) ||
       dev->sectors/(LFS_ERASE/SECTOR)>UINT32_MAX ||
       dev->sectors/(LFS_ERASE/SECTOR)<32U) {
        nas_block_put(dev);return -EOPNOTSUPP;
    }
    p->device=dev;
    p->config=(struct lfs_config){
        .context=p,.read=disk_read,.prog=disk_prog,.erase=disk_erase,
        .sync=disk_sync,.read_size=LFS_PROGRAM,.prog_size=LFS_PROGRAM,
        .block_size=LFS_ERASE,.block_count=(lfs_size_t)(dev->sectors/8U),
        .cache_size=LFS_PROGRAM,.lookahead_size=16,.block_cycles=100
    };
    return 0;
}
void nas_littlefs_block_detach(struct nas_littlefs_block *p) {
    if(!p || !p->device)return;
    nas_block_put(p->device);
    p->device=NULL;
}
