/* Real pinned lwext4 + NaSeed VFS + BlockDevice benchmark and correctness
 * oracle. A wrapped vendor directory iterator counts actual entries fetched,
 * independently of filesystem/page-cache hits and timer noise. */
#define main ext4_existing_vfs_fixture_main
#include "test-lwext4-vfs.c"
#undef main
#include <time.h>

static uint64_t directory_fetches;
const ext4_direntry *__real_ext4_dir_entry_next(ext4_dir *dir);
const ext4_direntry *__wrap_ext4_dir_entry_next(ext4_dir *dir) {
    directory_fetches++;
    return __real_ext4_dir_entry_next(dir);
}
static uint64_t elapsed_ns(void) {
    struct timespec ts;
    assert(clock_gettime(CLOCK_MONOTONIC,&ts)==0);
    return (uint64_t)ts.tv_sec*1000000000ULL+(uint64_t)ts.tv_nsec;
}
static void upstream_name_at(const char *directory,uint64_t ordinal,
                             char result[256]) {
    /* Independent upstream reference iterator: no NaSeed cursor hint. */
    ext4_dir d={0};
    assert(ext4_dir_open(&d,directory)==0);
    uint64_t seen=0;
    bool found=false;
    const ext4_direntry *entry;
    while((entry=ext4_dir_entry_next(&d))) {
        if(!entry->inode)continue;
        if(seen++!=ordinal)continue;
        assert(entry->name_length>0);
        memcpy(result,entry->name,entry->name_length);
        result[entry->name_length]=0;
        found=true;break;
    }
    assert(ext4_dir_close(&d)==0 && found);
}

int main(int argc,char **argv) {
    assert(argc==3);
    const bool optimized=!strcmp(argv[2],"optimized");
    assert(optimized || !strcmp(argv[2],"baseline"));
    FILE *disk=fopen(argv[1],"rb+");
    assert(disk && !fseek(disk,0,SEEK_END));
    long length=ftell(disk);
    assert(length>=8*1024*1024 && !fseek(disk,0,SEEK_SET));
    uint8_t *image=malloc((size_t)length);
    assert(image && fread(image,1,(size_t)length,disk)==(size_t)length);
    struct traced_disk physical={.image=image};
    physical.dev=(struct nas_block_device){.name="ext4-hot-bench",
        .sectors=(uint64_t)length/512U,.sector_bytes=512,
        .max_sectors_per_io=128,.ops=&trace_ops,.context=&physical};
    assert(nas_block_register(&physical.dev)==0);
    static struct nas_lwext4_mount mounted;
    assert(nas_lwext4_device_mount(&mounted,"ext4-hot-bench",
                                  "ext4-hot-adapter","/mnt",true)==0);
    /* The historical git baseline deliberately has no dir-cookie epoch. */
    const bool has_cursor_hint=mounted.namespace_epoch!=0;
    assert(nas_vfs_mkdir("/mnt/hot",0700)==0);
    assert(nas_vfs_mkdir("/mnt/hot/wide",0700)==0);
    char pathname[256],canonical[256];
    struct nas_fs_node node;
    for(unsigned i=0;i<320;i++) {
        assert(snprintf(pathname,sizeof(pathname),
                        "/mnt/hot/wide/f%03u",i)>0);
        assert(nas_vfs_create(pathname,0600)==0);
    }
    assert(nas_fs_lookup_node("/mnt/hot/wide",true,&node,canonical)==0);
    struct nas_vfs_dirent entries[7];
    uint64_t start=elapsed_ns();
    directory_fetches=0;
    unsigned batches=0,seen=0;
    uint64_t cursor=0;
    for(;;) {
        int count=nas_vfs_read_dir(&node,cursor,entries,7);
        assert(count>=0);
        if(!count)break;
        for(int j=0;j<count;j++) {
            if(cursor+(uint64_t)j>=2) {
                assert(entries[j].dtype==8);
                seen++;
            }
        }
        cursor+=(unsigned)count;
        batches++;
    }
    uint64_t directory_ns=elapsed_ns()-start;
    uint64_t directory_calls=directory_fetches;
    assert(seen==320);

    /* Two independent directory readers interleave forward and backward
     * seeks. The mount-local hint must be transparent to the logical VFS
     * cookie and must lose validity on rename/unlink/creation. */
    struct nas_fs_node second;
    assert(nas_fs_lookup_node("/mnt/hot/wide",true,&second,canonical)==0);
    assert(second.ino==node.ino);
    char expected[256];
    upstream_name_at("/mnt/hot/wide",7,expected);
    assert(nas_vfs_read_dir(&node,0,entries,7)==7);
    assert(nas_vfs_read_dir(&second,70,entries,7)==7);
    assert(nas_vfs_read_dir(&node,7,entries,1)==1);
    assert(!strcmp(entries[0].name,expected));
    assert(nas_vfs_read_dir(&second,0,entries,1)==1);
    assert(!strcmp(entries[0].name,"."));
    assert(nas_vfs_read_dir(&node,0,entries,7)==7);
    uint64_t epoch=mounted.namespace_epoch;
    assert(nas_vfs_rename("/mnt/hot/wide/f002",
                          "/mnt/hot/wide/renamed",0)==0);
    if(has_cursor_hint)assert(mounted.namespace_epoch!=epoch);
    upstream_name_at("/mnt/hot/wide",7,expected);
    assert(nas_vfs_read_dir(&second,7,entries,1)==1);
    assert(!strcmp(entries[0].name,expected));
    assert(nas_vfs_rename("/mnt/hot/wide/renamed",
                          "/mnt/hot/wide/f002",0)==0);
    assert(nas_vfs_read_dir(&node,0,entries,7)==7);
    epoch=mounted.namespace_epoch;
    assert(nas_vfs_unlink("/mnt/hot/wide/f003",0)==0);
    if(has_cursor_hint)assert(mounted.namespace_epoch!=epoch);
    assert(nas_vfs_create("/mnt/hot/wide/f003",0600)==0);
    upstream_name_at("/mnt/hot/wide",7,expected);
    assert(nas_vfs_read_dir(&second,7,entries,1)==1);
    assert(!strcmp(entries[0].name,expected));

    char deep[256]="/mnt/hot";
    for(unsigned i=0;i<32;i++) {
        size_t n=strlen(deep);
        assert(n+3<=sizeof(deep));
        deep[n]='/';deep[n+1]='d';deep[n+2]=0;
        assert(nas_vfs_mkdir(deep,0700)==0);
    }
    start=elapsed_ns();
    directory_fetches=0;
    for(unsigned i=0;i<20;i++) {
        assert(nas_fs_lookup_node(deep,true,&node,canonical)==0);
        assert(node.is_dir && !strcmp(canonical,deep));
    }
    uint64_t deep_ns=elapsed_ns()-start;
    uint64_t deep_calls=directory_fetches;
    /* Deterministic complexity guard: time is diagnostic only. The old
     * implementation fetched 7890/56740 entries on these exact workloads. */
    if(optimized) {
        assert(directory_calls<=650);
        assert(deep_calls<=6000);
    }
    struct nas_fs_node held=node,after_rename;
    assert(nas_vfs_node_pin(&held)==0);
    assert(nas_vfs_rename("/mnt/hot/d","/mnt/hot/moved",0)==0);
    assert(nas_fs_lookup_node(deep,true,&after_rename,canonical)==-ENOENT);
    char moved[256]="/mnt/hot/moved";
    for(unsigned i=1;i<32;i++) {
        size_t n=strlen(moved);
        moved[n]='/';moved[n+1]='d';moved[n+2]=0;
    }
    assert(nas_fs_lookup_node(moved,true,&after_rename,canonical)==0);
    assert(after_rename.ino==held.ino);
    assert(nas_vfs_getattr(&held,&after_rename)==0);
    assert(after_rename.ino==held.ino && after_rename.is_dir);
    assert(nas_vfs_node_unpin(&held)==0);
    char leaf[256];
    assert(snprintf(leaf,sizeof(leaf),"%s/file",moved)>0);
    assert(nas_vfs_create(leaf,0600)==0);
    assert(nas_fs_lookup_node(leaf,true,&node,canonical)==0);
    assert(nas_vfs_node_pin(&node)==0);
    static const uint8_t payload[]={1,2,3,4,5};
    uint8_t readback[sizeof(payload)]={0};
    assert(nas_vfs_write_at(&node,0,payload,sizeof(payload))==sizeof(payload));
    assert(nas_vfs_read_at(&node,0,readback,sizeof(readback))==sizeof(readback));
    assert(!memcmp(payload,readback,sizeof(payload)));
    assert(nas_vfs_node_unpin(&node)==0);
    assert(nas_vfs_unlink(leaf,0)==0);
    for(unsigned i=0;i<32;i++) {
        assert(nas_vfs_unlink(moved,0x200ULL)==0);
        moved[strlen(moved)-2]=0;
    }
    for(unsigned i=0;i<320;i++) {
        assert(snprintf(pathname,sizeof(pathname),
                        "/mnt/hot/wide/f%03u",i)>0);
        assert(nas_vfs_unlink(pathname,0)==0);
    }
    assert(nas_vfs_unlink("/mnt/hot/wide",0x200ULL)==0);
    assert(nas_vfs_unlink("/mnt/hot",0x200ULL)==0);
    assert(nas_lwext4_unmount(&mounted)==0);
    assert(nas_block_unregister(&physical.dev)==0);
    assert(!fseek(disk,0,SEEK_SET));
    assert(fwrite(image,1,(size_t)length,disk)==(size_t)length);
    assert(!fflush(disk) && !fclose(disk));
    free(image);
    printf("[EXT4 VFS HOTPATH BENCH] readdir files=%u batches=%u "
           "vendor_iterator_calls=%llu time_ns=%llu; "
           "deep_lookup depth=32 repeat=20 vendor_iterator_calls=%llu "
           "time_ns=%llu\n",seen,batches,
           (unsigned long long)directory_calls,(unsigned long long)directory_ns,
           (unsigned long long)deep_calls,(unsigned long long)deep_ns);
    printf("[EXT4 VFS HOTPATH CSV] %u,%u,%llu,%llu,%llu,%llu\n",
           seen,batches,(unsigned long long)directory_calls,
           (unsigned long long)directory_ns,
           (unsigned long long)deep_calls,(unsigned long long)deep_ns);
    return 0;
}
