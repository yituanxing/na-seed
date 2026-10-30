#include "partition.h"
#include "../errno.h"
#include "../lib/string.h"
#include <stdint.h>
#include <stddef.h>
/* MBR + primary GPT only. Not an extended-MBR chain or hybrid GPT editor.
 * All metadata is validated BEFORE a partition becomes a registered
 * device. The caller can choose among at most 128 GPT entries.
 * The 4KiB scratch belongs to this invocation, never a static global
 * shared by unrelated drivers or CPUs. */
static uint32_t le32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1]<<8) |
           ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);
}
static uint64_t le64(const uint8_t *b) {
    return (uint64_t)le32(b) | ((uint64_t)le32(b+4)<<32);
}
static uint32_t crc_step(uint32_t state,uint8_t value) {
    state^=value;
    for(unsigned b=0;b<8;b++)
        state=(state>>1) ^ (0xedb88320U & (0U-(state&1U)));
    return state;
}
static int probe_gpt(struct nas_block_device *disk,unsigned index,
                     struct nas_partition_extent *extent,uint8_t sector[4096]) {
    if(disk->sectors<4)return -EINVAL;
    int rc=nas_block_read(disk,1,1,sector);
    if(rc)return rc;
    if(memcmp(sector,"EFI PART",8))return -EINVAL;
    if(le32(sector+8)!=0x00010000U)return -EOPNOTSUPP;
    uint32_t header_bytes=le32(sector+12);
    if(header_bytes<92 || header_bytes>disk->sector_bytes ||
       le32(sector+20) || le64(sector+24)!=1)return -EINVAL;
    uint32_t header_crc=le32(sector+16),state=~0U;
    for(uint32_t i=0;i<header_bytes;i++)
        state=crc_step(state,i>=16 && i<20 ? 0 : sector[i]);
    if((~state)!=header_crc)return -EIO;
    uint64_t backup_lba=le64(sector+32);
    uint64_t first_usable=le64(sector+40),last_usable=le64(sector+48);
    uint64_t entries_lba=le64(sector+72);
    uint32_t entry_count=le32(sector+80),entry_bytes=le32(sector+84);
    uint32_t entries_crc=le32(sector+88);
    if(backup_lba<=1 || backup_lba>=disk->sectors ||
       first_usable<2 || last_usable>=backup_lba ||
       first_usable>last_usable || !entry_count ||
       entry_count>128 || entry_bytes<128 || entry_bytes>512 ||
       (entry_bytes&7U))return -EINVAL;
    if(index>entry_count)return -ENOENT;
    uint64_t bytes=(uint64_t)entry_count*entry_bytes;
    uint64_t sector_count=(bytes+disk->sector_bytes-1)/disk->sector_bytes;
    if(entries_lba<2 || entries_lba>disk->sectors ||
       sector_count>disk->sectors-entries_lba ||
       entries_lba+sector_count>first_usable)return -EINVAL;
    /* Validate EVERY used GPT entry, not only the one selected. A valid
     * CRC does not imply safe geometry: overlapping filesystem partitions
     * could otherwise alias one another's writable sectors. The bounded
     * table is checked in a single sector pass with no 64KiB allocation. */
    struct nas_partition_extent accepted[128];
    unsigned accepted_count=0;
    uint8_t record[512]={0};
    struct nas_partition_extent selected={0};
    bool selected_used=false;
    state=~0U;
    uint64_t offset=0;
    for(uint64_t i=0;i<sector_count;i++) {
        rc=nas_block_read(disk,entries_lba+i,1,sector);
        if(rc)return rc;
        uint32_t n=disk->sector_bytes;
        if(bytes-offset<n)n=(uint32_t)(bytes-offset);
        for(uint32_t j=0;j<n;j++) {
            uint64_t pos=offset+j;
            state=crc_step(state,sector[j]);
            uint32_t record_pos=(uint32_t)(pos%entry_bytes);
            record[record_pos]=sector[j];
            if(record_pos+1U!=entry_bytes)continue;
            unsigned number=(unsigned)(pos/entry_bytes)+1U;
            bool used=false;
            for(unsigned b=0;b<16;b++)
                if(record[b]) {used=true;break;}
            if(!used)continue;
            uint64_t first=le64(record+32);
            uint64_t last=le64(record+40);
            if(first<first_usable || last>last_usable || first>last)
                return -EINVAL;
            /* End-inclusive ranges overlap unless strictly disjoint.
             * The valid-entry list contains no empty GPT entries. */
            for(unsigned k=0;k<accepted_count;k++) {
                uint64_t prev_first=accepted[k].first_lba;
                uint64_t prev_last=prev_first+accepted[k].sectors-1U;
                if(first<=prev_last && prev_first<=last)return -EINVAL;
            }
            accepted[accepted_count++]=(struct nas_partition_extent){
                .first_lba=first,.sectors=last-first+1
            };
            if(number==index) {
                selected=accepted[accepted_count-1U];
                selected_used=true;
            }
        }
        offset+=n;
    }
    if((~state)!=entries_crc)return -EIO;
    if(!selected_used)return -ENOENT;
    *extent=selected;
    return 0;
}
int nas_partition_probe(struct nas_block_device *disk,unsigned index,
                        struct nas_partition_extent *extent) {
    if(!disk || !disk->registered || !extent)return -EINVAL;
    if(index<1 || index>128)return -EINVAL;
    if(disk->sector_bytes<512 || disk->sector_bytes>4096 ||
       disk->sectors<2)return -EINVAL;
    uint8_t sector[4096];
    int rc=nas_block_read(disk,0,1,sector);
    if(rc)return rc;
    if(sector[510]!=0x55 || sector[511]!=0xaa)return -EINVAL;
    bool protective=false,extra_mbr=false;
    for(unsigned n=0;n<4;n++) {
        uint8_t type=sector[446+n*16+4];
        if(type==0xee)protective=true;
        else if(type)extra_mbr=true;
    }
    /* Hybrid MBR/GPT is ambiguous: do not silently prefer one view of
     * overlapping storage. Normal protective MBR has one 0xee entry. */
    if(protective) {
        if(extra_mbr)return -EOPNOTSUPP;
        return probe_gpt(disk,index,extent,sector);
    }
    if(index>4)return -ENOENT;
    struct nas_partition_extent seen[4]={0};
    struct nas_partition_extent chosen={0};
    bool selected=false,unsupported=false;
    for(unsigned n=0;n<4;n++) {
        const uint8_t *entry=sector+446+n*16;
        uint8_t type=entry[4];
        if(!type)continue;
        if(entry[0]!=0 && entry[0]!=0x80)return -EINVAL;
        uint64_t first=le32(entry+8),size=le32(entry+12);
        if(!first || !size || first>=disk->sectors ||
           size>disk->sectors-first)return -EINVAL;
        for(unsigned k=0;k<n;k++)
            if(seen[k].sectors &&
               first<seen[k].first_lba+seen[k].sectors &&
               seen[k].first_lba<first+size)return -EINVAL;
        seen[n]=(struct nas_partition_extent){
            .first_lba=first,.sectors=size
        };
        if(n+1U==index) {
            selected=true;
            chosen=seen[n];
            unsupported=type==0x05 || type==0x0f || type==0x85;
        }
    }
    if(!selected)return -ENOENT;
    if(unsupported)return -EOPNOTSUPP;
    *extent=chosen;
    return 0;
}
static int partition_read(struct nas_block_device *block,uint64_t sector,
                          uint32_t count,void *dst) {
    struct nas_partition_view *view=(struct nas_partition_view *)block->context;
    return nas_block_read(view->parent,view->extent.first_lba+sector,count,dst);
}
static int partition_write(struct nas_block_device *block,uint64_t sector,
                           uint32_t count,const void *src) {
    struct nas_partition_view *view=(struct nas_partition_view *)block->context;
    return nas_block_write(view->parent,view->extent.first_lba+sector,count,src);
}
static int partition_flush(struct nas_block_device *block) {
    struct nas_partition_view *view=(struct nas_partition_view *)block->context;
    return nas_block_flush(view->parent);
}
static const struct nas_block_ops partition_ops={
    .read=partition_read,.write=partition_write,.flush=partition_flush
};
int nas_partition_attach(struct nas_partition_view *view,const char *name,
                         const char *disk_name,
                         const struct nas_partition_extent *extent,
                         bool read_only) {
    if(!view || !name || !name[0] || !disk_name || !extent ||
       view->block.registered || view->block.refs || view->parent)
        return -EINVAL;
    struct nas_block_device *disk=nas_block_get(disk_name);
    if(!disk)return -ENODEV;
    if(!extent->sectors || extent->first_lba>=disk->sectors ||
       extent->sectors>disk->sectors-extent->first_lba) {
        nas_block_put(disk);
        return -EINVAL;
    }
    /* This view cannot advertise a larger I/O request than its parent.
     * Parent references protect nested partitions and driver hot removal. */
    struct nas_block_device next={
        .name=name,.sectors=extent->sectors,
        .sector_bytes=disk->sector_bytes,
        .max_sectors_per_io=disk->max_sectors_per_io,
        .read_only=read_only||disk->read_only,
        .ops=&partition_ops,.context=view
    };
    view->parent=disk;
    view->extent=*extent;
    view->block=next;
    int rc=nas_block_register(&view->block);
    if(rc) {
        view->block=(struct nas_block_device){0};
        view->extent=(struct nas_partition_extent){0};
        view->parent=NULL;
        nas_block_put(disk);
    }
    return rc;
}
int nas_partition_detach(struct nas_partition_view *view) {
    if(!view || !view->parent)return -ENODEV;
    int rc=nas_block_unregister(&view->block);
    if(rc)return rc;
    struct nas_block_device *parent=view->parent;
    view->block=(struct nas_block_device){0};
    view->extent=(struct nas_partition_extent){0};
    view->parent=NULL;
    nas_block_put(parent);
    return 0;
}
