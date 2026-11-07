/* A real upstream lwext4 + NaSeed BlockDevice + RAM-backed ext4 image test.
 * A second invocation verifies persistent bytes after a fresh mount. */
#include <ext4.h>
#include "../ports/lwext4/lwext4_block.h"
#include "../kernel/block/ramdisk.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static void check(int rc,const char *action) {
    if(rc) {fprintf(stderr,"[LWEXT4] %s failed: %d\n",action,rc);exit(1);}
}
int main(int argc,char **argv) {
    if(argc!=3 || (strcmp(argv[2],"write")&&strcmp(argv[2],"verify")&&
                   strcmp(argv[2],"sync-crash")&&strcmp(argv[2],"sync-reopen")&&
                   strcmp(argv[2],"sparse-verify")))return 2;
    bool sync_crash=!strcmp(argv[2],"sync-crash");
    bool sync_reopen=!strcmp(argv[2],"sync-reopen");
    bool sparse_verify=!strcmp(argv[2],"sparse-verify");
    bool write=!sparse_verify && strcmp(argv[2],"verify")!=0;
    FILE *disk=fopen(argv[1],"rb+");
    assert(disk);
    assert(!fseek(disk,0,SEEK_END));
    long bytes=ftell(disk);
    assert(bytes>1048576 && (bytes%512)==0 && bytes<128L*1024L*1024L);
    rewind(disk);
    uint8_t *image=malloc((size_t)bytes);
    assert(image);
    assert(fread(image,1,(size_t)bytes,disk)==(size_t)bytes);
    struct nas_ramdisk rd={0};
    struct nas_lwext4_device device={0};
    check(nas_ramdisk_attach(&rd,"image0",image,(uint64_t)bytes,512,write),
          "attach memory device");
    check(nas_lwext4_device_attach(&device,"image0","ext-test"),
          "register upstream lwext4 device");
    assert(nas_ramdisk_detach(&rd)==-EBUSY); /* real reference pin */
    check(ext4_mount("ext-test","/mnt/",!write),"mount ext4");
    if(sparse_verify) {
        /* The host has deliberately created a 32-KiB sparse inode with NO
         * data blocks. Every logical file byte must be zero, even when a
         * request starts at a full-block boundary. Older lwext4 passed the
         * physical block number zero to ext4_blocks_get_direct, returning
         * the ext4 superblock instead. This broke genuine Alpine libcrypto
         * when the dynamic linker mmap'd one 4-KiB sparse page. */
        ext4_file f={0};
        check(ext4_fopen(&f,"/mnt/sparse-hole","r"),"open sparse inode");
        assert(ext4_fsize(&f)==32768);
        static const struct { uint64_t offset; size_t length; } cases[]={
            {0,4096},       /* aligned single whole sparse block */
            {4096,8192},    /* two contiguous whole sparse blocks */
            {4096,4096+37}, /* whole sparse block + trailing partial */
            {103,1536},    /* unaligned partial sparse block */
            {4096-11,4096+37}, /* initial partial + whole + tail */
            {32768-63,63}  /* final partial sparse block */
        };
        unsigned char buf[8192];
        for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
            check(ext4_fseek(&f,(int64_t)cases[i].offset,SEEK_SET),
                  "seek sparse hole");
            memset(buf,0xa5,sizeof(buf));
            size_t got=0;
            check(ext4_fread(&f,buf,cases[i].length,&got),"read sparse hole");
            assert(got==cases[i].length);
            for(size_t j=0;j<got;j++)if(buf[j]) {
                fprintf(stderr,"[LWEXT4 SPARSE READ FAIL] case=%zu file_off=%llu value=0x%02x\n",
                        i,(unsigned long long)(cases[i].offset+j),buf[j]);
                exit(1);
            }
        }
        check(ext4_fclose(&f),"close sparse inode");
        check(ext4_umount("/mnt/"),"unmount sparse ext4");
        check(nas_lwext4_device_detach(&device,"ext-test"),"detach sparse ext4");
        check(nas_ramdisk_detach(&rd),"detach sparse RAM disk");
        assert(!fclose(disk));
        free(image);
        puts("[LWEXT4 SPARSE READ PASS] entire aligned blocks and partial sparse holes are zero, not ext4 block0");
        return 0;
    }
    if(sync_crash || sync_reopen) {
        static const char witness[]="NaSeed original ext4 ordered-sync crash witness\n";
        ext4_file f={0};
        if(sync_crash) {
            check(ext4_fopen(&f,"/mnt/sync-witness","w+"),"create sync witness");
            size_t got=0;
            check(ext4_fwrite(&f,witness,sizeof(witness)-1,&got),"write sync witness");
            assert(got==sizeof(witness)-1);
            check(ext4_fclose(&f),"close sync witness");
            /* Persist the in-memory superblock free counters on a mounted
             * filesystem, then simulate power removal by NEVER umounting.
             * Dumping RAM-backed physical media models a completed device
             * flush. A separate process must remount this exact image. */
            check(ext4_sync("/mnt/"),"ordered mounted ext4 sync");
            rewind(disk);
            assert(fwrite(image,1,(size_t)bytes,disk)==(size_t)bytes);
            assert(!fflush(disk));
            assert(!fclose(disk));
            puts("[LWEXT4 ORDERED SYNC] committed while mounted; omitted umount");
            return 0;
        }
        check(ext4_fopen(&f,"/mnt/sync-witness","r"),"reopen sync witness");
        char buf[sizeof(witness)]={0};
        size_t got=0;
        check(ext4_fread(&f,buf,sizeof(witness)-1,&got),"read sync witness");
        assert(got==sizeof(witness)-1);
        assert(!memcmp(buf,witness,sizeof(witness)-1));
        check(ext4_fclose(&f),"close sync witness");
        check(ext4_umount("/mnt/"),"clean second mount unmount");
        check(nas_lwext4_device_detach(&device,"ext-test"),"detach ext4 device");
        check(nas_ramdisk_detach(&rd),"detach RAM disk");
        rewind(disk);
        assert(fwrite(image,1,(size_t)bytes,disk)==(size_t)bytes);
        assert(!fflush(disk));
        assert(!fclose(disk));
        free(image);
        puts("[LWEXT4 ORDERED SYNC REOPEN PASS] original mounted write survived crash");
        return 0;
    }
    static const char payload[]="NaSeed VFS and BlockDevice real lwext4 data\n";
    if(write) {
        check(ext4_dir_mk("/mnt/sandbox"),"mkdir");
        ext4_file f={0};
        check(ext4_fopen(&f,"/mnt/sandbox/original","w+"),"create file");
        size_t got=0;
        check(ext4_fwrite(&f,payload,sizeof(payload)-1,&got),"write file");
        assert(got==sizeof(payload)-1);
        check(ext4_fclose(&f),"close written file");
        check(ext4_frename("/mnt/sandbox/original","/mnt/sandbox/renamed"),
              "rename file");
        check(ext4_flink("/mnt/sandbox/renamed","/mnt/sandbox/hardlink"),
              "hard link");
        check(ext4_fremove("/mnt/sandbox/renamed"),"unlink one name");
    }
    ext4_file f={0};
    check(ext4_fopen(&f,"/mnt/sandbox/hardlink","r"),"open hardlink");
    char buf[128]={0};
    size_t got=0;
    check(ext4_fread(&f,buf,sizeof(payload)-1,&got),"read hardlink");
    assert(got==sizeof(payload)-1);
    assert(!memcmp(buf,payload,sizeof(payload)-1));
    check(ext4_fclose(&f),"close hardlink");
    check(ext4_umount("/mnt/"),"unmount filesystem");
    check(nas_lwext4_device_detach(&device,"ext-test"),"unregister device");
    check(nas_ramdisk_detach(&rd),"detach RAM image");
    if(write) {
        rewind(disk);
        assert(fwrite(image,1,(size_t)bytes,disk)==(size_t)bytes);
        assert(!fflush(disk));
    }
    assert(!fclose(disk));
    free(image);
    puts(write ? "[LWEXT4 REAL WRITE PASS]" : "[LWEXT4 REMOUNT PASS]");
    return 0;
}
