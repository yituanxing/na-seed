/* Actual unmodified littlefs v2.11.2 on the same NaSeed BlockDevice
 * backing RAM that lwext4 uses: independent filesystem control variable.
 * Not a fake VFS mount; tests the library->BlockDevice contract first. */
#include "../ports/littlefs/block_adapter.h"
#include "../kernel/block/ramdisk.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
int main(void) {
    const size_t image_size=2U*1024U*1024U;
    uint8_t *image=malloc(image_size);assert(image);
    memset(image,0xff,image_size);
    struct nas_ramdisk ram={0};
    assert(nas_ramdisk_attach(&ram,"littlefs-ram",image,image_size,512,true)==0);
    struct nas_littlefs_block bridge={0};
    assert(nas_littlefs_block_attach(&bridge,"littlefs-ram")==0);
    assert(nas_ramdisk_detach(&ram)==-16); /* pinned BlockDevice */
    lfs_t fs={0};
    assert(lfs_format(&fs,&bridge.config)==0);
    assert(lfs_mount(&fs,&bridge.config)==0);
    assert(lfs_mkdir(&fs,"/work")==0);
    lfs_file_t file={0};
    assert(lfs_file_open(&fs,&file,"/work/sample",LFS_O_CREAT|LFS_O_RDWR)==0);
    uint8_t expected[1536];
    for(size_t i=0;i<sizeof(expected);i++)expected[i]=(uint8_t)(i*13U+7U);
    assert(lfs_file_write(&fs,&file,expected,sizeof(expected))==(lfs_ssize_t)sizeof(expected));
    assert(lfs_file_sync(&fs,&file)==0);
    assert(lfs_file_close(&fs,&file)==0);
    assert(lfs_rename(&fs,"/work/sample","/work/renamed")==0);
    struct lfs_info meta={0};
    assert(lfs_stat(&fs,"/work/renamed",&meta)==0);
    assert(meta.type==LFS_TYPE_REG && meta.size==sizeof(expected));
    assert(lfs_stat(&fs,"/work/sample",&meta)==LFS_ERR_NOENT);
    lfs_dir_t dir={0};
    assert(lfs_dir_open(&fs,&dir,"/work")==0);
    bool found=false;int n;
    while((n=lfs_dir_read(&fs,&dir,&meta))>0)
        if(!strcmp(meta.name,"renamed"))found=true;
    assert(n==0 && found);
    assert(lfs_dir_close(&fs,&dir)==0);
    assert(lfs_unmount(&fs)==0);
    /* Full independent filesystem remount on the original storage bytes. */
    memset(&fs,0,sizeof(fs));
    assert(lfs_mount(&fs,&bridge.config)==0);
    assert(lfs_file_open(&fs,&file,"/work/renamed",LFS_O_RDONLY)==0);
    uint8_t check[sizeof(expected)]={0};
    assert(lfs_file_read(&fs,&file,check,sizeof(check))==(lfs_ssize_t)sizeof(check));
    assert(!memcmp(expected,check,sizeof(expected)));
    assert(lfs_file_close(&fs,&file)==0);
    assert(lfs_remove(&fs,"/work/renamed")==0);
    assert(lfs_remove(&fs,"/work")==0);
    assert(lfs_unmount(&fs)==0);
    nas_littlefs_block_detach(&bridge);
    assert(nas_ramdisk_detach(&ram)==0);
    free(image);
    puts("[UPSTREAM LITTLEFS BLOCK PASS] genuine BSD-3 littlefs format/create/write/sync/rename/readdir/remount/read/remove on shared BlockDevice");
    return 0;
}
