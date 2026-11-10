/* Real Limine boot module -> kernel VFS -> lwext4 -> RamDisk path.
 * This is an opt-in source-level capability, never a replacement for the
 * common VFS or userspace syscall route. Build for all supported ISAs. */
#include "guest_vfs.h"
#ifdef NAS_ENABLE_M180_FS_USER_IRQ
#include "../../kernel/fs/broker.h"
#endif
#include "../../kernel/fs/lookup.h"
#include "../../kernel/fs/vfs.h"
#include "../../kernel/errno.h"
#include "../../kernel/lib/string.h"
#include <limine.h>
#include <stdint.h>
#include <stdbool.h>
#if defined(NAS_ENABLE_M191_ALPINE_ROOT) || \
    defined(NAS_ENABLE_H8_QEMU_ALPINE_ROOT) || \
    defined(NAS_ENABLE_H9_QEMU_AHCI_USERLAND)
#define NAS_EXTERNAL_EXT4_BOOT_ROOT 1
#endif
#ifdef NAS_ENABLE_M179_FS_IRQ
#include "../../kernel/mm/heap.h"
#include "../../kernel/task/kthread.h"
#include "../../kernel/panic.h"
#include "../uboot/async_blk.h"
#endif
#if defined(NAS_ENABLE_VIRTIO_BLK) || \
    defined(NAS_ENABLE_H7_QEMU_NVME_USERLAND) || \
    defined(NAS_ENABLE_H9_QEMU_AHCI_USERLAND)
#include "../../kernel/block/device.h"
#include "../../kernel/block/partition.h"
#ifdef NAS_ENABLE_VIRTIO_BLK
extern int nas_platform_virtio_blk_probe(void);
#endif
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
#ifdef NAS_EXTERNAL_EXT4_BOOT_ROOT
extern void nas_dec(uint64_t);
static void m191_boot_fail(const char *phase,int code) {
#ifdef NAS_ENABLE_H8_QEMU_ALPINE_ROOT
    nas_print("[H8 NVME ROOT FAIL] ");
#elif defined(NAS_ENABLE_H9_QEMU_AHCI_USERLAND)
    nas_print("[H9 AHCI ROOT FAIL] ");
#else
    nas_print("[M191 ROOT FAIL] ");
#endif
    nas_print(phase);
    nas_print(" code=");nas_dec((uint64_t)(code<0?-code:code));
    nas_print("\n");
}
#endif
static bool boot_shutdown_failed(const char *stage,int code) {
#ifdef NAS_EXTERNAL_EXT4_BOOT_ROOT
    nas_print("[LWEXT4 SHUTDOWN ERROR] ");nas_print(stage);
    nas_print(" errno=");nas_dec((uint64_t)(code<0?-code:code));
    nas_print("\n");
#else
    (void)stage;(void)code;
#endif
    return false;
}
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
#ifdef NAS_EXTERNAL_EXT4_BOOT_ROOT
        /* The persistent Alpine root must be backed by a real partition.
         * A Limine RAM image cannot satisfy the reboot-persistence gate. */
        return false;
#endif
        bool ram_rw=ephemeral_rw_module(file);
        int rc=nas_lwext4_ramdisk_mount_mode(&boot_mount,"limine-ext4",
                                             "limine-ext4-dev","/mnt",
                                             file->address,file->size,ram_rw);
        if(rc)return false;
        nas_print("[LWEXT4 GUEST] actual Limine ext4 image mounted at /mnt\n");
        if(ram_rw)
            nas_print("[LWEXT4 GUEST] explicit ephemeral RW RamDisk only; NOT persistent storage\n");
    } else {
#if defined(NAS_ENABLE_VIRTIO_BLK) || \
    defined(NAS_ENABLE_H7_QEMU_NVME_USERLAND) || \
    defined(NAS_ENABLE_H9_QEMU_AHCI_USERLAND)
        /* Bus/transport discovery happens below the FS, not via a
         * syscall/backend-specific path. The EFI ESP is also VirtIO-Blk
         * but is FAT; select the *actual ext4 superblock* from each disk. */
#if defined(NAS_ENABLE_H7_QEMU_NVME_USERLAND) || \
    defined(NAS_ENABLE_H9_QEMU_AHCI_USERLAND)
        /* H7 is a separate, QEMU-qualified, permanently owned NVMe session.
         * Its readonly BlockDevice was attached in H1/H3 discovery, before
         * the full boot moves into VFS. A real PC must never use this path. */
        int disks=1;
#else
        int disks=nas_platform_virtio_blk_probe();
#endif
        if(disks<0) {
#ifdef NAS_ENABLE_M191_ALPINE_ROOT
            m191_boot_fail("virtio-probe",disks);
#endif
            return false;
        }
        unsigned found=0;
        char chosen_disk[16]={0};
        unsigned chosen_partition_index=0;
        struct nas_partition_extent chosen_extent={0};
        bool chosen_writable=false;
        for(int i=0;i<disks;i++){
#ifdef NAS_ENABLE_H7_QEMU_NVME_USERLAND
            const char block_name[]="nvme0";
#elif defined(NAS_ENABLE_H9_QEMU_AHCI_USERLAND)
            const char block_name[]="ahci0";
#else
            char block_name[]="virtio0";
            block_name[6]=(char)('0'+i);
#endif
            struct nas_block_device *block=nas_block_get(block_name);
            if(!block) {
#ifdef NAS_ENABLE_M191_ALPINE_ROOT
                m191_boot_fail("block-device-lookup",i);
#endif
                return false;
            }
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
        if(!found) {
#ifdef NAS_ENABLE_H7_QEMU_NVME_USERLAND
            nas_print("[H7 NVME EXT4 FAIL] no genuine ext4 superblock on nvme0\n");
            return false;
#elif defined(NAS_ENABLE_H9_QEMU_AHCI_USERLAND)
            nas_print("[H9 AHCI ROOT FAIL] no genuine ext4 superblock on ahci0\n");
            return false;
#endif
#ifdef NAS_EXTERNAL_EXT4_BOOT_ROOT
            m191_boot_fail("ext4-superblock-not-found",disks);
            return false; /* selected external root is mandatory */
#else
            return true; /* legitimate boot without ext4 disk */
#endif
        }
        const char *selected_device=chosen_disk;
        static char partition_name[32];
        if(chosen_partition_index) {
            partition_device_name(partition_name,chosen_disk,
                                  chosen_partition_index);
            int rc=nas_partition_attach(&boot_partition,partition_name,
                                        chosen_disk,&chosen_extent,false);
            if(rc) {
#ifdef NAS_ENABLE_M191_ALPINE_ROOT
                m191_boot_fail("partition-attach",rc);
#endif
                return false;
            }
            boot_partition_attached=true;
            selected_device=partition_name;
        }
        const char *target=
#ifdef NAS_EXTERNAL_EXT4_BOOT_ROOT
            "/";
#else
            "/mnt";
#endif
#if defined(NAS_ENABLE_H7_QEMU_NVME_USERLAND) || \
    defined(NAS_ENABLE_H9_QEMU_AHCI_USERLAND)
        if(chosen_writable) {
#ifdef NAS_ENABLE_H9_QEMU_AHCI_USERLAND
            nas_print("[H9 AHCI ROOT FAIL] SATA BlockDevice unexpectedly writable\n");
#else
            nas_print("[H7 NVME EXT4 FAIL] QEMU NVMe BlockDevice unexpectedly writable\n");
#endif
            return false;
        }
#endif
        int mount_rc=nas_lwext4_device_mount(&boot_mount,selected_device,
#ifdef NAS_ENABLE_H7_QEMU_NVME_USERLAND
                                           "h7-nvme-ext4",
#elif defined(NAS_ENABLE_H9_QEMU_AHCI_USERLAND)
                                           "h9-ahci-ext4",
#else
                                           "virtio-ext4",
#endif
                                           target,chosen_writable);
        if(mount_rc) {
#ifdef NAS_ENABLE_M191_ALPINE_ROOT
            m191_boot_fail("lwext4-device-mount",mount_rc);
#endif
            if(boot_partition_attached) {
                (void)nas_partition_detach(&boot_partition);
                boot_partition_attached=false;
            }
            return false;
        }
#ifdef NAS_EXTERNAL_EXT4_BOOT_ROOT
        /* The M191 build switch now also hosts signed Debian/glibc root
         * programs. Keep its old acceptance marker for Alpine regressions,
         * and provide a distribution-neutral physical root observation. */
        nas_print("[ROOTFS] physical ext4 root registered at /\n");
#ifdef NAS_ENABLE_M191_ALPINE_ROOT
        nas_print("[M191 ROOT] physical Alpine ext4 registered at /\n");
#endif
#endif
#ifdef NAS_ENABLE_H7_QEMU_NVME_USERLAND
        if(!boot_mount.mount.read_only) {
            nas_print("[H7 NVME EXT4 FAIL] mounted filesystem must be read-only\n");
            return false;
        }
#ifdef NAS_ENABLE_H8_QEMU_ALPINE_ROOT
        nas_print("[H8 NVME ALPINE ROOT MOUNTED] / read-only source=nvme0\n");
#else
        nas_print("[H7 NVME EXT4 MOUNTED] /mnt read-only source=nvme0\n");
#endif
#elif defined(NAS_ENABLE_H9_QEMU_AHCI_USERLAND)
        if(!boot_mount.mount.read_only) {
            nas_print("[H9 AHCI ROOT FAIL] mounted filesystem must be read-only\n");
            return false;
        }
        nas_print("[H9 AHCI ROOT MOUNTED] / read-only source=ahci0\n");
#else
        nas_print(chosen_partition_index ?
            "[LWEXT4 GUEST] real GPT/MBR partition ext4 disk mounted\n" :
            "[LWEXT4 GUEST] real external VirtIO-Blk ext4 disk mounted\n");
        if(!boot_mount.mount.read_only)
            nas_print("[VIRTIO-BLK] persistent block device write+flush enabled\n");
#endif
#else
        return true; /* No boot RAM image and physical driver not enabled */
#endif
    }
    char path[256];
    struct nas_fs_node node;
    /* Same VFS pathwalk whether the provider owns a RAM module or real disk. */
    const char *mounted_path=
#ifdef NAS_EXTERNAL_EXT4_BOOT_ROOT
        "/";
#else
        "/mnt";
#endif
    if(nas_fs_lookup_node(mounted_path,true,&node,path) || !node.is_dir ||
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
    if (!boot_mount.mounted) return true;
#if !defined(NAS_ENABLE_H7_QEMU_NVME_USERLAND) && \
    !defined(NAS_ENABLE_H9_QEMU_AHCI_USERLAND)
    /* Legacy Limine readonly boot modules are kept for the kernel lifetime.
     * H7 must drop its physical NVMe filesystem lease before controller
     * shutdown and therefore executes the full readonly unmount below. */
    if (boot_mount.mount.read_only) return true;
#endif
#ifdef NAS_EXTERNAL_EXT4_BOOT_ROOT
    /* Original OpenRC mounts real runtime tmpfs (such as /run), which
     * retains parent/covered root Dentries even after PID1 and its shell
     * exit. Quiescence has already closed every File and cwd owner here. */
    int rc=nas_vfs_shutdown_runtime_tmpfs();
    if(rc)return boot_shutdown_failed("runtime tmpfs teardown",rc);
    rc=nas_vfs_shutdown_external_root_pseudos();
    if(rc)return boot_shutdown_failed("pseudo-mount teardown",rc);
#endif
    /* A read-only image cannot have dirty metadata to commit and some
     * upstream read-only close paths refuse redundant writeback. Only a
     * writable filesystem requires final ext4 superblock commit. */
    const bool was_read_only=boot_mount.mount.read_only;
    int unmount_rc=nas_lwext4_unmount(&boot_mount);
    if(unmount_rc)return boot_shutdown_failed("root ext4 unmount",unmount_rc);
#if defined(NAS_ENABLE_VIRTIO_BLK) || \
    defined(NAS_ENABLE_H7_QEMU_NVME_USERLAND) || \
    defined(NAS_ENABLE_H9_QEMU_AHCI_USERLAND)
    /* The mounted filesystem has dropped its BlockDevice reference.
     * Only NOW may the partition release its parent physical device. */
    if(boot_partition_attached) {
        int detach_rc=nas_partition_detach(&boot_partition);
        if(detach_rc)return boot_shutdown_failed("partition detach",detach_rc);
        boot_partition_attached=false;
    }
#endif
    nas_print(was_read_only ?
        "[LWEXT4 GUEST] read-only upstream unmount; no media writes/flush\n" :
        "[LWEXT4 GUEST] clean upstream unmount and durable flush\n");
    return true;
}

#ifdef NAS_ENABLE_M179_FS_IRQ
/* M179: the *same* upstream lwext4+VFS pipeline as boot ext4, now on a
 * second physical VirtIO-BLK partition whose BlockDevice may park the
 * owning kernel C frame until an actual hardware IRQ completes its DMA.
 * The entire mount/test/unmount lifetime stays in ONE kernel worker. In
 * particular, exposing ordinary user open/create/stat on this mount before
 * an M180 control-plane broker would enter that blocking driver on a user
 * Task stack, so VFS rejects foreign access via mount.worker_io. */
#define M179_SEED_BYTES 8192U
#define M179_WRITE_BYTES 12288U
#define M179_BLOCK_BYTES 4096U
enum m179_state { M179_DORMANT, M179_SPAWNED, M179_RUNNING,
                  M179_PASSED, M179_FAILED };
static enum m179_state m179_state;
static struct nas_kthread_handle m179_worker;
static struct nas_lwext4_mount m179_mount;
#ifdef NAS_ENABLE_M180_FS_USER_IRQ
enum m180_state { M180_DORMANT, M180_SPAWNED, M180_MOUNTED,
                  M180_UNMOUNTING, M180_UNMOUNTED };
static enum m180_state m180_state;
static struct nas_kthread_handle m180_worker;
static bool m180_shutdown_requested;
#endif
static const char m179_seed_header[]="M179_HOST_EXT4_SEED";
static const char m179_write_header[]="M179_GUEST_EXT4_WRITE";

bool nas_lwext4_async_worker_current(void) {
    if((m179_state==M179_RUNNING || m179_state==M179_PASSED) &&
       nas_kthread_is_current(m179_worker))return true;
#ifdef NAS_ENABLE_M180_FS_USER_IRQ
    return (m180_state==M180_SPAWNED || m180_state==M180_MOUNTED ||
            m180_state==M180_UNMOUNTING) &&
           nas_kthread_is_current(m180_worker);
#else
    return false;
#endif
}
bool nas_lwext4_async_runtime_passed(void) {
    return m179_state==M179_PASSED;
}
/* The same kernel image may boot without the independent IRQ-backed disk.
 * DORMANT means no filesystem worker/fixture was armed on THIS boot, not a
 * worker that has failed or is still waiting for its durable flush. */
bool nas_lwext4_async_runtime_armed(void) {
    return m179_state!=M179_DORMANT;
}
static uint8_t m179_expected_byte(uint64_t offset,bool guest) {
    const char *header=guest?m179_write_header:m179_seed_header;
    size_t header_length=guest?sizeof(m179_write_header):
                               sizeof(m179_seed_header);
    if(offset<header_length)return (uint8_t)header[offset];
    uint64_t fill=offset-header_length;
    return (uint8_t)((fill*31U+(guest?43U:17U))&255U);
}
static bool m179_bytes_match(const uint8_t *bytes,uint64_t start,
                             uint64_t count,bool guest) {
    for(uint64_t i=0;i<count;i++)
        if(bytes[i]!=m179_expected_byte(start+i,guest))return false;
    return true;
}
static void m179_print_stats(const struct nas_async_blk_stats *before,
                              const struct nas_async_blk_stats *after) {
    nas_print("[M179 FS IRQ WORKER STATS] request_submitted=");
    nas_dec(after->submitted-before->submitted);
    nas_print(" request_completed=");
    nas_dec(after->completed-before->completed);
    nas_print(" irq_ack=");nas_dec(after->irq_ack-before->irq_ack);
    nas_print(" deferred=");nas_dec(after->deferred-before->deferred);
    nas_print(" fallback_poll=");
    nas_dec(after->fallback_poll-before->fallback_poll);
    nas_print(" waited=");
    nas_dec(after->worker_waits-before->worker_waits);
    nas_print("\n");
}
static bool m179_verified_ext4(void) {
    struct nas_async_blk_stats before={0},after={0};
    nas_async_blk_get_stats(&before);
    int rc=nas_async_blk_register_block_device();
    if(rc)return false;
    const char *partition=nas_async_blk_mountable_partition();
    if(!partition || strcmp(partition,"async-fixturep1"))return false;
    m179_mount.worker_io_mount=true;
    rc=nas_lwext4_device_mount(&m179_mount,partition,"m179-ext4-dev",
                                "/async",true);
    if(rc)return false;
    bool okay=false;
    uint8_t *buffer=nas_heap_alloc(M179_BLOCK_BYTES);
    if(!buffer)goto Done;
    struct nas_fs_node inode={0};
    rc=nas_vfs_lookup("/async/host-seed.bin",&inode);
    if(rc || inode.size!=M179_SEED_BYTES)goto DoneBuffer;
    if(nas_vfs_node_pin(&inode))goto DoneBuffer;
    for(uint64_t off=0;off<M179_SEED_BYTES;off+=M179_BLOCK_BYTES) {
        int64_t got=nas_vfs_read_at(&inode,off,buffer,M179_BLOCK_BYTES);
        if(got!=M179_BLOCK_BYTES ||
           !m179_bytes_match(buffer,off,M179_BLOCK_BYTES,false)) {
            (void)nas_vfs_node_unpin(&inode);
            goto DoneBuffer;
        }
    }
    if(nas_vfs_node_unpin(&inode))goto DoneBuffer;
    rc=nas_vfs_create("/async/guest-write.bin",0644);
    /* M190's next real kernel boot retains this same ext4 partition.
     * Only our own deterministic fixture inode may already exist; still
     * overwrite, fsync, reopen, and compare every byte. Never turn other
     * errors into a false M179 worker PASS. */
    if(rc && rc!=-EEXIST)goto DoneBuffer;
    rc=nas_vfs_lookup("/async/guest-write.bin",&inode);
    if(rc || nas_vfs_node_pin(&inode))goto DoneBuffer;
    for(uint64_t off=0;off<M179_WRITE_BYTES;off+=M179_BLOCK_BYTES) {
        for(unsigned i=0;i<M179_BLOCK_BYTES;i++)
            buffer[i]=m179_expected_byte(off+i,true);
        int64_t wrote=nas_vfs_write_at(&inode,off,buffer,M179_BLOCK_BYTES);
        if(wrote!=M179_BLOCK_BYTES) {
            (void)nas_vfs_node_unpin(&inode);
            goto DoneBuffer;
        }
    }
    int synced=nas_vfs_sync(&inode);
    int released=nas_vfs_node_unpin(&inode);
    if(synced || released)goto DoneBuffer;
    /* Fresh VFS pathname/inode resolution, not an aliased dirty source
     * buffer: prove the vendor filesystem can reopen the physical bytes. */
    rc=nas_vfs_lookup("/async/guest-write.bin",&inode);
    if(rc || nas_vfs_node_size(&inode)!=M179_WRITE_BYTES ||
       nas_vfs_node_pin(&inode))goto DoneBuffer;
    okay=true;
    for(uint64_t off=0;off<M179_WRITE_BYTES;off+=M179_BLOCK_BYTES) {
        int64_t got=nas_vfs_read_at(&inode,off,buffer,M179_BLOCK_BYTES);
        if(got!=M179_BLOCK_BYTES ||
           !m179_bytes_match(buffer,off,M179_BLOCK_BYTES,true)) {
            okay=false;
            break;
        }
    }
    if(nas_vfs_node_unpin(&inode))okay=false;
DoneBuffer:
    if(!nas_heap_free(buffer))okay=false;
Done:
    /* The final journal/superblock flush is part of the proof. An fsync
     * without ext4_umount is NOT an ext4 filesystem clean-shutdown oracle. */
    if(nas_lwext4_unmount(&m179_mount))okay=false;
    nas_async_blk_get_stats(&after);
    m179_print_stats(&before,&after);
    if(after.submitted<=before.submitted ||
       after.completed<=before.completed ||
       after.irq_ack<=before.irq_ack ||
       after.deferred<=before.deferred ||
       after.worker_waits<=before.worker_waits ||
       after.fallback_poll!=before.fallback_poll)okay=false;
    return okay;
}
static void m179_worker_entry(void *argument) {
    (void)argument;
    if(m179_state!=M179_SPAWNED)
        nas_fail("M179 worker started without exactly-once ownership");
    m179_state=M179_RUNNING;
    if(!m179_verified_ext4()) {
        m179_state=M179_FAILED;
        nas_fail("M179 real ext4 IRQ-backed worker mount/read/write/fsync/umount failed");
    }
    m179_state=M179_PASSED;
    nas_print("[M179 FS IRQ WORKER PASS]\n");
    nas_print("[M179] real lwext4 VFS 8KiB host read + "
              "12KiB durable guest write/reopen; BlockRequest IRQ and clean unmount\n");
}
#ifdef NAS_ENABLE_M180_FS_USER_IRQ
/* M179's original independent disk oracle must finish AND cleanly unmount
 * before M180 may attach its user-facing instance. Start the latter after
 * M179 PASS, independently of user requests: pathname traversal must see
 * the newly published /async mount before it can know to queue an openat. */
static void m180_worker_entry(void *argument) {
    (void)argument;
    if(m179_state!=M179_PASSED || m180_state!=M180_SPAWNED ||
       !nas_kthread_is_current(m180_worker))
        nas_fail("M180 filesystem worker started without exclusive owner");
    struct nas_async_blk_stats before={0},after={0};
    nas_async_blk_get_stats(&before);
    const char *partition=nas_async_blk_mountable_partition();
    if(!partition || strcmp(partition,"async-fixturep1"))
        nas_fail("M180 original physical ext4 partition unavailable");
    m179_mount.worker_io_mount=true;
    int rc=nas_lwext4_device_mount(&m179_mount,partition,"m179-ext4-dev",
                                    "/async",true);
    if(rc)nas_fail("M180 persistent IRQ-backed lwext4 remount failed");
    m180_state=M180_MOUNTED;
    nas_print("[M180 FS USER IRQ] remounted /async in sole filesystem worker\n");
    /* One serialized VFS executor. Its own C stack parks on real DMA IRQ;
     * a blocked userspace syscall has an entirely different Task stack. */
    nas_fs_broker_serve();
    m180_state=M180_UNMOUNTING;
    if(nas_lwext4_unmount(&m179_mount))
        nas_fail("M180 final ext4 unmount/durable flush failed");
    nas_async_blk_get_stats(&after);
    struct nas_fs_broker_stats broker={0};
    nas_fs_broker_get_stats(&broker);
    nas_print("[M180 FS USER IRQ STATS] submitted=");
    nas_dec(broker.submitted);
    nas_print(" started=");nas_dec(broker.started);
    nas_print(" completed=");nas_dec(broker.completed);
    nas_print(" detached=");nas_dec(broker.detached);
    nas_print(" abandoned=");nas_dec(broker.abandoned);
    nas_print(" peak_queued=");nas_dec(broker.peak_queued);
    nas_print(" broker_submitted=");nas_dec(broker.submitted);
    nas_print(" broker_completed=");nas_dec(broker.completed);
    nas_print(" task_parked=");nas_dec(broker.task_parked);
    nas_print(" task_woken=");nas_dec(broker.task_woken);
    nas_print(" request_submitted=");nas_dec(after.submitted-before.submitted);
    nas_print(" request_completed=");nas_dec(after.completed-before.completed);
    nas_print(" irq_ack=");nas_dec(after.irq_ack-before.irq_ack);
    nas_print(" deferred=");nas_dec(after.deferred-before.deferred);
    nas_print(" fallback_poll=");
    nas_dec(after.fallback_poll-before.fallback_poll);
    nas_print(" worker_waited=");nas_dec(after.worker_waits-before.worker_waits);
    nas_print("\n[M180 FS USER IRQ] clean unmount and durable flush\n");
    m180_state=M180_UNMOUNTED;
}
bool nas_lwext4_m180_runtime_ready(void) {
    return m179_state==M179_PASSED && !m180_shutdown_requested &&
           m180_state==M180_MOUNTED;
}
void nas_lwext4_m180_shutdown_request(void) {
    m180_shutdown_requested=true;
    nas_fs_broker_request_shutdown();
}
bool nas_lwext4_m180_shutdown_complete(void) {
    if(m179_state!=M179_PASSED)return false;
    return m180_state==M180_UNMOUNTED &&
           nas_fs_broker_shutdown_complete();
}
#endif
void nas_lwext4_async_runtime_step(void) {
    if(m179_state==M179_DORMANT && nas_async_blk_fixture_ready()) {
        if(!nas_kthread_spawn(m179_worker_entry,NULL,&m179_worker))
            nas_fail("M179 cannot reserve owned filesystem kernel worker");
        m179_state=M179_SPAWNED;
    }
#ifdef NAS_ENABLE_M180_FS_USER_IRQ
    if(m179_state==M179_PASSED && m180_state==M180_DORMANT) {
        if(!nas_kthread_spawn(m180_worker_entry,NULL,&m180_worker))
            nas_fail("M180 cannot reserve persistent filesystem worker");
        m180_state=M180_SPAWNED;
    }
#endif
}
#endif
