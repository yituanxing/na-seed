/* Produce the SAME original littlefs raw disk accepted by VFS host tests
 * for the separate real QEMU native Linux-ELF syscall probe. */
#include "../ports/littlefs/block_adapter.h"
#include "../kernel/block/ramdisk.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    const size_t n=2*1024*1024;
    uint8_t *bytes=malloc(n);assert(bytes);
    memset(bytes,0xff,n);
    struct nas_ramdisk ram={0};
    assert(nas_ramdisk_attach(&ram,"littlefs-image-builder",bytes,n,512,true)==0);
    struct nas_littlefs_block adapter={0};
    assert(nas_littlefs_block_attach(&adapter,"littlefs-image-builder")==0);
    lfs_t fs={0};
    assert(lfs_format(&fs,&adapter.config)==0);
    assert(lfs_mount(&fs,&adapter.config)==0);
    assert(lfs_mkdir(&fs,"/source")==0);
    lfs_file_t file={0};
    static const char data[]="Second independent real filesystem through NaSeed VFS";
    assert(lfs_file_open(&fs,&file,"/source/program",LFS_O_CREAT|LFS_O_WRONLY)==0);
    assert(lfs_file_write(&fs,&file,data,sizeof(data)-1)==
           (lfs_ssize_t)sizeof(data)-1);
    assert(lfs_file_close(&fs,&file)==0);
    assert(lfs_unmount(&fs)==0);
    nas_littlefs_block_detach(&adapter);
    assert(nas_ramdisk_detach(&ram)==0);
    FILE *out=fopen(argv[1],"wb");assert(out);
    assert(fwrite(bytes,1,n,out)==n && fclose(out)==0);
    free(bytes);
    return 0;
}
