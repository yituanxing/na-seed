/* Real Limine boot module -> kernel VFS -> lwext4 -> RamDisk path.
 * This is an opt-in source-level capability, never a replacement for the
 * common VFS or userspace syscall route. Build for all supported ISAs. */
#include "guest_vfs.h"
#include "../../kernel/fs/lookup.h"
#include "../../kernel/fs/vfs.h"
#include "../../kernel/lib/string.h"
#include <limine.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef NAS_ENABLE_VIRTIO_BLK
#include "../../kernel/block/device.h"
#include "../../kernel/block/partition.h"
extern int nas_platform_virtio_blk_probe(void);
static struct nas_partition_view boot_partition;
static bool boot_partition_attached;
/* Inspect an ext4 superblock at byte 1080 relative to the BlockDevice
 * candidate. Discovery alone reads a *whole disk*; filesystem I/O will
 * use ONLY the registered child view for partitioned devices. */
static bool physical_ext4_at(struct nas_block_device *disk,uint64_t first) {
    if(!disk || !disk->sector_bytes || disk->sector_bytes>4096)
        return false;
    const uint64_t magic=1024+56;
    uint64_t offset=magic/disk->sector_bytes;
    if(first>=disk->sectors || offset>=disk->sectors-first)
        return false;
    uint8_t sector[4096];
    if(nas_block_read(disk,first+offset,1,sector))return false;
    unsigned at=(unsigned)(magic%disk->sector_bytes);
    return sector[at]==0x53 && sector[at+1]==0xef;
}
static void partition_device_name(char name[32],
                                  const char *disk,unsigned index) {
    size_t n=strlen(disk);
    memcpy(name,disk,n);
    name[n++]='p';
    if(index>=100)name[n++]=(char)('0'+index/100);
    if(index>=10)name[n++]=(char)('0'+(index/10)%10);
    name[n++]=(char)('0'+index%10);
    name[n]=0;
}
#endif

extern const struct limine_file *nas_module(uint64_t index);
extern void nas_print(const char *);
static struct nas_lwext4_mount boot_mount;

static bool ext4_module(const struct limine_file *file) {
    if(!file || !file->address || file->size < 1082 ||
       file->size > 128U*1024U*1024U || (file->size & 511U))return false;
    const uint8_t *data=(const uint8_t *)file->address;
    /* On-disk ext superblock magic at 1024+0x38, not a module filename. */
    return data[1080]==0x53 && data[1081]==0xef;
}
/* The boot loader's module basename is an explicit RAM-only RW opt-in.
 * No physical drive or persistent-disk write capability is implied.
 * Ordinary "ext4.img" boot modules stay strictly read-only. */
static bool ephemeral_rw_module(const struct limine_file *file) {
    if(!file || !file->path)return false;
    const char *last=strrchr(file->path,'/');
    const char *name=last ? last+1 : file->path;
    return !strcmp(name,"naseed-ram-rw.ext4");
}
bool nas_lwext4_guest_boot_mount(void) {
    const struct limine_file *file=NULL;
    for(uint64_t i=1;i<64;i++) {
        const struct limine_file *next=nas_module(i);
        if(!next)break;
        if(ext4_module(next)) {
            if(file)return false; /* More than one boot disk needs config. */
            file=next;
        }
    }
    if(file) {
        bool ram_rw=ephemeral_rw_module(file);
        int rc=nas_lwext4_ramdisk_mount_mode(&boot_mount,"limine-ext4",
                                             "limine-ext4-dev","/mnt",
                                             file->address,file->size,ram_rw);
        if(rc)return false;
        nas_print("[LWEXT4 GUEST] actual Limine ext4 image mounted at /mnt\n");
        if(ram_rw)
            nas_print("[LWEXT4 GUEST] explicit ephemeral RW RamDisk only; NOT persistent storage\n");
    } else {
#ifdef NAS_ENABLE_VIRTIO_BLK
        /* Bus/transport discovery happens below the FS, not via a
         * syscall/backend-specific path. The EFI ESP is also VirtIO-Blk
         * but is FAT; select the *actual ext4 superblock* from each disk. */
        int disks=nas_platform_virtio_blk_probe();
        if(disks<0)return false;
        unsigned found=0;
        char chosen_disk[16]={0};
        unsigned chosen_partition_index=0;
        struct nas_partition_extent chosen_extent={0};
        bool chosen_writable=false;
        for(int i=0;i<disks;i++){
            char block_name[]="virtio0";
            block_name[6]=(char)('0'+i);
            struct nas_block_device *block=nas_block_get(block_name);
            if(!block)return false;
            bool whole_ext4=physical_ext4_at(block,0);
            if(whole_ext4) {
                if(found++) {nas_block_put(block);return false;}
                memcpy(chosen_disk,block_name,sizeof(block_name));
                chosen_partition_index=0;
                chosen_writable=!block->read_only;
            } else {
                /* Partition metadata belongs to the generic block layer.
                 * Check independent GPT/MBR entries before selecting an
                 * actual mountable ext4; do NOT make lwext4 parse tables.
                 * The first ext4 partition on a disk is the boot volume
                 * until explicit per-disk selection is configured. */
                for(unsigned part=1;part<=128;part++) {
                    struct nas_partition_extent extent={0};
                    int rc=nas_partition_probe(block,part,&extent);
                    if(rc==-ENOENT)continue;
                    if(rc==-EINVAL || rc==-EOPNOTSUPP)break;
                    if(rc) {nas_block_put(block);return false;}
                    if(!physical_ext4_at(block,extent.first_lba))continue;
                    if(found++) {nas_block_put(block);return false;}
                    memcpy(chosen_disk,block_name,sizeof(block_name));
                    chosen_partition_index=part;
                    chosen_extent=extent;
                    chosen_writable=!block->read_only;
                    break;
                }
            }
            nas_block_put(block);
        }
        if(!found)return true; /* legitimate boot without ext4 disk */
        const char *selected_device=chosen_disk;
        static char partition_name[32];
        if(chosen_partition_index) {
            partition_device_name(partition_name,chosen_disk,
                                  chosen_partition_index);
            int rc=nas_partition_attach(&boot_partition,partition_name,
                                        chosen_disk,&chosen_extent,false);
            if(rc)return false;
            boot_partition_attached=true;
            selected_device=partition_name;
        }
        if(nas_lwext4_device_mount(&boot_mount,selected_device,
                                   "virtio-ext4","/mnt",chosen_writable)) {
            if(boot_partition_attached) {
                (void)nas_partition_detach(&boot_partition);
                boot_partition_attached=false;
            }
            return false;
        }
        nas_print(chosen_partition_index ?
            "[LWEXT4 GUEST] real GPT/MBR partition ext4 disk mounted at /mnt\n" :
            "[LWEXT4 GUEST] real external VirtIO-Blk ext4 disk mounted at /mnt\n");
        if(!boot_mount.mount.read_only)
            nas_print("[VIRTIO-BLK] persistent block device write+flush enabled\n");
#else
        return true; /* No boot RAM image and physical driver not enabled */
#endif
    }
    char path[256];
    struct nas_fs_node node;
    /* Same VFS pathwalk whether the provider owns a RAM module or real disk. */
    if(nas_fs_lookup_node("/mnt",true,&node,path) || !node.is_dir ||
       !node.filesystem || strcmp(node.filesystem->name,"lwext4"))
        return false;

    /* The acceptance fixture is optional for arbitrary user-supplied disks.
     * A separate real-QEMU gate requires this marker from the known image. */
    static const char expected[]="NaSeed VFS and BlockDevice real lwext4 data\n";
    if(nas_fs_lookup_node("/mnt/sandbox/hardlink",true,&node,path)==0) {
        uint8_t contents[sizeof(expected)];
        if(node.size!=sizeof(expected)-1 ||
           nas_vfs_read_at(&node,0,contents,sizeof(contents))!=(int64_t)node.size ||
           memcmp(contents,expected,sizeof(expected)-1))
            return false;
        nas_print("[PASS] LWEXT4 guest VFS real ext4 inode read\n");
    }
    return true;
}

/* The real guest shutdown path after PID1 has exited and closed its Files.
 * Preserve the same generic VFS->lwext4->BlockDevice unmount on all ISAs.
 * Without ext4_umount, the journal/superblock free counters can lag even
 * after fsync and block-cache flush; QEMU termination is not an unmount. */
bool nas_lwext4_guest_boot_shutdown(void) {
    if (!boot_mount.mounted || boot_mount.mount.read_only) return true;
    /* A read-only image cannot have dirty metadata to commit and some
     * upstream read-only close paths refuse redundant writeback. Only a
     * writable filesystem requires final ext4 superblock commit. */
    if (nas_lwext4_unmount(&boot_mount)) return false;
#ifdef NAS_ENABLE_VIRTIO_BLK
    /* The mounted filesystem has dropped its BlockDevice reference.
     * Only NOW may the partition release its parent physical device. */
    if(boot_partition_attached) {
        if(nas_partition_detach(&boot_partition))return false;
        boot_partition_attached=false;
    }
#endif
    nas_print("[LWEXT4 GUEST] clean upstream unmount and durable flush\n");
    return true;
}
