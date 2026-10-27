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
    if(argc!=3 || (strcmp(argv[2],"write")&&strcmp(argv[2],"verify")))return 2;
    bool write=!strcmp(argv[2],"write");
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
