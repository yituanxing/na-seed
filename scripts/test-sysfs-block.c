/* Exercise the production sysfs/devfs providers with the same complete
 * kernel VFS fixtures, then register real block-class owners and a validated
 * MBR partition. No alternate/fake sysfs implementation is linked. */
#define main nas_vfs_fixture_main
#include "test-vfs-provider.c"
#undef main

#include "../kernel/block/device.h"
#include "../kernel/block/partition.h"

static uint8_t sysfs_mbr[4096];
static int sysfs_read(struct nas_block_device *dev,uint64_t lba,
                      uint32_t count,void *dst) {
    assert(count<=dev->max_sectors_per_io);
    memset(dst,0,(size_t)count*dev->sector_bytes);
    if(lba==0)memcpy(dst,sysfs_mbr,dev->sector_bytes);
    return 0;
}
static int sysfs_write(struct nas_block_device *dev,uint64_t lba,
                       uint32_t count,const void *src) {
    (void)dev;(void)lba;(void)count;(void)src;
    return 0;
}
static int sysfs_flush(struct nas_block_device *dev) {(void)dev;return 0;}
static const struct nas_block_ops sysfs_ops={
    .read=sysfs_read,.write=sysfs_write,.flush=sysfs_flush
};
static struct nas_fs_node sysfs_lookup(const char *path,bool follow) {
    char resolved[256];struct nas_fs_node node={0};
    int rc=nas_fs_lookup_node(path,follow,&node,resolved);
    if(rc)fprintf(stderr,"[BLOCK SYSFS FAIL] lookup %s rc %d\n",path,rc);
    assert(rc==0);
    return node;
}
static void sysfs_expect(const char *path,const char *expected) {
    struct nas_fs_node node=sysfs_lookup(path,true);
    char result[256]={0};
    int64_t n=nas_vfs_read_at(&node,0,(uint8_t *)result,sizeof(result)-1U);
    assert(n==(int64_t)strlen(expected));
    assert(!memcmp(result,expected,(size_t)n));
    assert(nas_vfs_read_at(&node,(uint64_t)n,(uint8_t *)result,1)==0);
}
static void sysfs_link(const char *path,const char *expected) {
    struct nas_fs_node node=sysfs_lookup(path,false);
    assert(node.is_symlink);
    char target[256]={0};
    int rc=nas_vfs_readlink(&node,target);
    assert(rc==(int)strlen(expected));
    assert(!strcmp(target,expected));
}
int main(void) {
    assert(nas_vfs_fixture_main()==0);
    char resolved[256];struct nas_fs_node node;
    struct nas_vfs_dirent entries[24];
    node=sysfs_lookup("/sys/dev/block",true);
    assert(nas_vfs_read_dir(&node,0,entries,24)==2);
    node=sysfs_lookup("/sys/block",true);
    assert(nas_vfs_read_dir(&node,0,entries,24)==2);
    assert(nas_fs_lookup_node("/sys/dev/block/240:0",false,&node,resolved)==-ENOENT);

    sysfs_mbr[510]=0x55;sysfs_mbr[511]=0xaa;
    sysfs_mbr[446+4]=0x83;
    sysfs_mbr[446+8]=8;  /* real first LBA, LE32 */
    sysfs_mbr[446+12]=200;/* real sector count, LE32 */
    struct nas_block_device disk={
        .name="virtio0",.sectors=1000,.sector_bytes=4096,
        .max_sectors_per_io=1,.ops=&sysfs_ops
    };
    assert(nas_block_register(&disk)==0);
    assert(disk.rdev==(NAS_BLOCK_MAJOR<<8));
    node=sysfs_lookup("/dev/virtio0",true);
    assert((node.mode&0170000U)==0060000U && node.rdev==disk.rdev);
    sysfs_link("/sys/block/virtio0","../devices/virtual/block/virtio0");
    sysfs_link("/sys/dev/block/240:0","../../devices/virtual/block/virtio0");
    node=sysfs_lookup("/sys/dev/block/240:0",true);
    assert(node.is_dir);
    sysfs_expect("/sys/block/virtio0/size","8000\n");
    sysfs_expect("/sys/block/virtio0/dev","240:0\n");
    sysfs_expect("/sys/block/virtio0/queue/logical_block_size","4096\n");
    sysfs_expect("/sys/block/virtio0/queue/physical_block_size","4096\n");
    assert(nas_fs_lookup_node("/sys/block/virtio0/partition",true,
                              &node,resolved)==-ENOENT);

    struct nas_partition_extent extent={0};
    assert(nas_partition_probe(&disk,1,&extent)==0);
    assert(extent.first_lba==8 && extent.sectors==200);
    struct nas_partition_view part={0};
    assert(nas_partition_attach(&part,"virtio0p1","virtio0",&extent,true)==0);
    assert(part.block.parent==&disk && part.block.partition_number==1);
    assert(part.block.rdev==((NAS_BLOCK_MAJOR<<8)|1U));
    sysfs_link("/sys/dev/block/240:1",
               "../../devices/virtual/block/virtio0/virtio0p1");
    sysfs_expect("/sys/block/virtio0/virtio0p1/partition","1\n");
    sysfs_expect("/sys/block/virtio0/virtio0p1/start","64\n");
    sysfs_expect("/sys/block/virtio0/virtio0p1/size","1600\n");
    sysfs_expect("/sys/block/virtio0/virtio0p1/ro","1\n");
    node=sysfs_lookup("/sys/dev/block",true);
    assert(nas_vfs_read_dir(&node,0,entries,24)==4);
    assert(!strcmp(entries[2].name,"240:0") && entries[2].dtype==10);
    assert(!strcmp(entries[3].name,"240:1") && entries[3].dtype==10);
    struct nas_fs_node stale=sysfs_lookup("/sys/dev/block/240:1",false);
    assert(nas_partition_detach(&part)==0);
    assert(nas_block_unregister(&disk)==0);
    assert(nas_vfs_readlink(&stale,resolved)<0);
    node=sysfs_lookup("/sys/dev/block",true);
    assert(nas_vfs_read_dir(&node,0,entries,24)==2);
    puts("[BLOCK SYSFS PASS] real registry, devfs S_IFBLK, symlink topology, geometry, partitions, removal");
    return 0;
}
