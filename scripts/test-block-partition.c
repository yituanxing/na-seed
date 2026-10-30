/* Genuine BlockDevice/Partition view and real MBR/GPT byte layouts.
 * No direct filesystem table, mock syscall, or second device registry. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/block/partition.h"
#include "../kernel/block/ramdisk.h"
#include "../kernel/errno.h"

static uint8_t disk[512U*256U];
static uint8_t check[4096];
static struct nas_ramdisk parent;
static struct nas_partition_view part;
static void wr32(uint8_t *p,uint32_t x) {
    for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(x>>(8U*i));
}
static void wr64(uint8_t *p,uint64_t x) {
    wr32(p,(uint32_t)x);wr32(p+4,(uint32_t)(x>>32));
}
static uint32_t crc(const uint8_t *p,size_t n) {
    uint32_t c=~0U;
    for(size_t j=0;j<n;j++) {
        c^=p[j];
        for(unsigned b=0;b<8;b++)
            c=(c>>1) ^ (0xedb88320U & (0U-(c&1U)));
    }
    return ~c;
}
static void protective_gpt(unsigned sector_bytes,unsigned sectors) {
    memset(disk,0,sizeof(disk));
    uint8_t *mbr=disk,*header=disk+sector_bytes;
    mbr[510]=0x55;mbr[511]=0xaa;
    mbr[446+4]=0xee;
    wr32(mbr+446+8,1);wr32(mbr+446+12,sectors-1);
    const uint32_t nentries=128,entry_bytes=128;
    const uint64_t table_bytes=(uint64_t)nentries*entry_bytes;
    const uint64_t table_sectors=(table_bytes+sector_bytes-1)/sector_bytes;
    const uint64_t first_usable=2+table_sectors;
    const uint64_t first=first_usable+2,last=first+15;
    uint8_t *entries=disk+2*sector_bytes;
    entries[0]=0xa2;entries[1]=0xa0; /* real nonzero type GUID */
    entries[16]=0x31;entries[17]=0x41; /* unique partition GUID */
    wr64(entries+32,first);wr64(entries+40,last);
    memcpy(header,"EFI PART",8);
    wr32(header+8,0x00010000);wr32(header+12,92);
    wr64(header+24,1);wr64(header+32,sectors-1);
    wr64(header+40,first_usable);wr64(header+48,sectors-2);
    wr64(header+72,2);wr32(header+80,nentries);
    wr32(header+84,entry_bytes);
    wr32(header+88,crc(entries,(size_t)table_bytes));
    wr32(header+16,crc(header,92));
}
static void verify_gpt(unsigned sector_bytes,unsigned sectors) {
    protective_gpt(sector_bytes,sectors);
    assert(nas_ramdisk_attach(&parent,"gpt",disk,
                              (uint64_t)sectors*sector_bytes,
                              sector_bytes,true)==0);
    struct nas_partition_extent e={0};
    assert(nas_partition_probe(&parent.block,1,&e)==0);
    assert(e.first_lba>2 && e.sectors==16);
    assert(nas_partition_probe(&parent.block,2,&e)==-ENOENT);
    assert(nas_partition_probe(&parent.block,129,&e)==-EINVAL);
    struct nas_partition_extent invalid={
        .first_lba=sectors-2,.sectors=8
    };
    assert(nas_partition_attach(&part,"gpt1","gpt",&invalid,false)==-EINVAL);
    assert(nas_partition_probe(&parent.block,1,&e)==0);
    assert(nas_partition_attach(&part,"gpt1","gpt",&e,false)==0);
    assert(parent.block.refs==1);
    struct nas_block_device *held=nas_block_get("gpt1");
    assert(held==&part.block);
    assert(nas_partition_detach(&part)==-EBUSY);
    assert(nas_ramdisk_detach(&parent)==-EBUSY);
    memset(check,0xb7,sector_bytes);
    assert(nas_block_write(held,0,1,check)==0);
    assert(!memcmp(disk+(size_t)e.first_lba*sector_bytes,
                   check,sector_bytes));
    memset(check,0,sector_bytes);
    assert(nas_block_read(held,0,1,check)==0 && check[0]==0xb7);
    assert(nas_block_read(held,16,1,check)==-EINVAL);
    assert(nas_block_write(held,16,1,check)==-EINVAL);
    assert(nas_block_flush(held)==0);
    nas_block_put(held);
    assert(nas_partition_detach(&part)==0);
    assert(parent.block.refs==0);
    /* Corrupted GPT content may never publish a partition range. */
    disk[2*sector_bytes+1]^=1;
    assert(nas_partition_probe(&parent.block,1,&e)==-EIO);
    disk[2*sector_bytes+1]^=1;
    disk[sector_bytes+80]^=1;
    assert(nas_partition_probe(&parent.block,1,&e)==-EIO);
    /* Two INDEPENDENT genuine GPT entries with recomputed CRCs. This
     * confirms the parser examines ALL used entries rather than only the
     * requested partition, and rejects overlap even when CRC is valid. */
    protective_gpt(sector_bytes,sectors);
    uint8_t *header=disk+sector_bytes;
    uint8_t *entries=disk+2*sector_bytes;
    uint8_t *second=entries+128;
    memcpy(second,entries,128);
    second[16]^=0x22; /* unique on-disk partition GUID */
    const uint64_t first=e.first_lba;
    wr64(second+32,first+16);wr64(second+40,first+19);
    wr32(header+88,crc(entries,128*128));
    wr32(header+16,0);wr32(header+16,crc(header,92));
    struct nas_partition_extent other={0};
    assert(nas_partition_probe(&parent.block,2,&other)==0);
    assert(other.first_lba==first+16 && other.sectors==4);
    wr64(second+32,first+8);wr64(second+40,first+11);
    wr32(header+88,crc(entries,128*128));
    wr32(header+16,0);wr32(header+16,crc(header,92));
    assert(nas_partition_probe(&parent.block,1,&e)==-EINVAL);
    assert(nas_partition_probe(&parent.block,2,&e)==-EINVAL);
    /* The primary GPT's usable LBAs must not overlap backup metadata. */
    protective_gpt(sector_bytes,sectors);
    header=disk+sector_bytes;
    wr64(header+48,sectors-1);
    wr32(header+16,0);wr32(header+16,crc(header,92));
    assert(nas_partition_probe(&parent.block,1,&e)==-EINVAL);
    assert(nas_ramdisk_detach(&parent)==0);
}
int main(void) {
    memset(disk,0,sizeof(disk));
    disk[510]=0x55;disk[511]=0xaa;
    uint8_t *one=disk+446;
    one[4]=0x83;wr32(one+8,8);wr32(one+12,31);
    uint8_t *two=one+16;two[4]=0x0f;wr32(two+8,40);wr32(two+12,50);
    assert(nas_ramdisk_attach(&parent,"mbr",disk,sizeof(disk),512,true)==0);
    struct nas_partition_extent e={0};
    assert(nas_partition_probe(&parent.block,1,&e)==0);
    assert(e.first_lba==8 && e.sectors==31);
    assert(nas_partition_probe(&parent.block,2,&e)==-EOPNOTSUPP);
    assert(nas_partition_probe(&parent.block,3,&e)==-ENOENT);
    assert(nas_partition_probe(&parent.block,5,&e)==-ENOENT);
    /* A legal MBR signature cannot authorize two overlapping
     * BlockDevice views into the same writable physical sectors. */
    wr32(two+8,30);
    assert(nas_partition_probe(&parent.block,1,&e)==-EINVAL);
    wr32(two+8,40);
    two[4]=0xee;
    assert(nas_partition_probe(&parent.block,1,&e)==-EOPNOTSUPP);
    two[4]=0x0f;
    struct nas_partition_extent invalid={
        .first_lba=UINT64_MAX,.sectors=31
    };
    assert(nas_partition_attach(&part,"mbr1","mbr",&invalid,false)==-EINVAL);
    e=(struct nas_partition_extent){.first_lba=8,.sectors=31};
    assert(nas_partition_attach(&part,"mbr1","mbr",&e,true)==0);
    assert(nas_block_write(&part.block,0,1,check)==-EROFS);
    assert(nas_partition_detach(&part)==0);
    assert(nas_partition_attach(&part,"mbr1","mbr",&e,false)==0);
    assert(nas_partition_attach(&part,"dup","mbr",&e,false)==-EINVAL);
    assert(nas_partition_detach(&part)==0);
    assert(nas_ramdisk_detach(&parent)==0);
    verify_gpt(512,256);
    verify_gpt(4096,32);
    puts("[PARTITION PASS] real MBR/GPT CRC, geometry, non-overlap, 512+4096, view I/O, error, RO, flush, parent refs");
    return 0;
}
