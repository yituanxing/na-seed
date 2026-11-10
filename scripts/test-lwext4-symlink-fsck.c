/* Real pinned lwext4 + NaSeed RAM BlockDevice symlink/fsck regression.
 * A separate process remounts the physical bytes to verify every target.
 * The shell wrapper provides a strict e2fsck oracle between both processes. */
#include <ext4.h>
#include <ext4_fs.h>
#include "../ports/lwext4/lwext4_block.h"
#include "../kernel/block/ramdisk.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POISON_BLOCKS 4096U
#define BLOCK_BYTES 4096U

/* Put 4095 first: its single allocated data block should reuse a poisoned
 * block from a large, freshly removed regular file. The remaining lengths
 * cover both sides of the 60-byte fast-symlink boundary. */
static const unsigned lengths[]={4095,54,59,60,61,62,67,69,71};

static void check(int rc,const char *what) {
    if(rc){fprintf(stderr,"[SYMLINK FSCK FAIL] %s: %d\n",what,rc);exit(1);}
}
static void target_for(unsigned len,char target[4096]) {
    assert(len && len<4096);
    for(unsigned i=0;i<len;i++)target[i]=(char)('a'+(i%26));
    target[len]=0;
}
static void path_for(unsigned len,char path[128]) {
    int n=snprintf(path,128,"/mnt/certs/hash-%u.0",len);
    assert(n>0 && n<128);
}
static void readback(unsigned len) {
    char target[4096],actual[4096];
    char path[128];
    target_for(len,target);
    path_for(len,path);
    memset(actual,0xa5,sizeof(actual));
    size_t obtained=0;
    check(ext4_readlink(path,actual,sizeof(actual),&obtained),"readlink");
    if(obtained!=len || memcmp(actual,target,len)) {
        fprintf(stderr,"[SYMLINK FSCK FAIL] length=%u read=%zu mismatch\n",
                len,obtained);
        exit(1);
    }
}
static uint32_t inode_for(const char *directory,const char *name) {
    ext4_dir d={0};
    check(ext4_dir_open(&d,directory),"open directory");
    const ext4_direntry *entry;
    uint32_t found=0;
    const size_t wanted=strlen(name);
    while((entry=ext4_dir_entry_next(&d))) {
        if(entry->inode && entry->name_length==wanted &&
           !memcmp(entry->name,name,wanted)) {
            found=entry->inode;
            break;
        }
    }
    check(ext4_dir_close(&d),"close directory");
    if(!found){fprintf(stderr,"[SYMLINK FSCK FAIL] missing %s\n",name);exit(1);}
    return found;
}
static ext4_fsblk_t physical_block(struct ext4_fs *fs,uint32_t ino,
                                   uint32_t logical) {
    struct ext4_inode_ref ref={0};
    ext4_fsblk_t physical=0;
    check(ext4_fs_get_inode_ref(fs,ino,&ref),"get physical inode ref");
    int rc=ext4_fs_get_inode_dblk_idx(&ref,logical,&physical,true);
    int put=ext4_fs_put_inode_ref(&ref);
    check(rc,"map inode block");
    check(put,"put physical inode ref");
    return physical;
}
static void poison_and_remove(struct ext4_fs *fs,
                              ext4_fsblk_t poisoned[POISON_BLOCKS]) {
    ext4_file f={0};
    const char *path="/mnt/certs/nonzero-reuse-witness";
    check(ext4_fopen(&f,path,"w+"),"create poison file");
    uint8_t bytes[BLOCK_BYTES];
    memset(bytes,0xa5,sizeof(bytes));
    for(unsigned i=0;i<POISON_BLOCKS;i++) {
        size_t written=0;
        check(ext4_fwrite(&f,bytes,sizeof(bytes),&written),"write poison block");
        if(written!=sizeof(bytes)){
            fputs("[SYMLINK FSCK FAIL] short poison write\n",stderr);exit(1);
        }
    }
    const uint32_t ino=f.inode;
    for(unsigned i=0;i<POISON_BLOCKS;i++) {
        poisoned[i]=physical_block(fs,ino,i);
        if(!poisoned[i]){
            fputs("[SYMLINK FSCK FAIL] poison file has a hole\n",stderr);exit(1);
        }
    }
    check(ext4_fclose(&f),"close poison file");
    check(ext4_fremove(path),"unlink poison file");
}
static void create_all(struct ext4_fs *fs,
                       const ext4_fsblk_t poisoned[POISON_BLOCKS],
                       bool check_reuse) {
    for(size_t i=0;i<sizeof(lengths)/sizeof(lengths[0]);i++) {
        char path[128],target[4096];
        unsigned len=lengths[i];
        path_for(len,path);
        target_for(len,target);
        check(ext4_fsymlink(target,path),"create symlink");
        readback(len);
        if(check_reuse && len==4095) {
            uint32_t ino=inode_for("/mnt/certs","hash-4095.0");
            ext4_fsblk_t block=physical_block(fs,ino,0);
            bool reused=false;
            for(unsigned j=0;j<POISON_BLOCKS;j++)
                if(block==poisoned[j]){reused=true;break;}
            if(!block || !reused) {
                fprintf(stderr,"[SYMLINK FSCK FAIL] nonzero-block reuse "
                               "not established: link block=%llu\n",
                        (unsigned long long)block);
                exit(1);
            }
            printf("[SYMLINK PHYSICAL REUSE PASS] 4095-byte link block=%llu\n",
                   (unsigned long long)block);
        }
        printf("[SYMLINK CREATE PASS] target_bytes=%u\n",len);
    }
}
static void verify_all(void) {
    for(size_t i=0;i<sizeof(lengths)/sizeof(lengths[0]);i++) {
        readback(lengths[i]);
        printf("[SYMLINK READLINK PASS] target_bytes=%u\n",lengths[i]);
    }
}

int main(int argc,char **argv) {
    if(argc!=3 || (strcmp(argv[2],"create") && strcmp(argv[2],"reopen")))
        return 2;
    bool create=!strcmp(argv[2],"create");
    FILE *disk=fopen(argv[1],"rb+");
    assert(disk && !fseek(disk,0,SEEK_END));
    long bytes=ftell(disk);
    assert(bytes>1048576 && bytes%512==0 && bytes<128L*1024L*1024L);
    rewind(disk);
    uint8_t *image=malloc((size_t)bytes);
    assert(image && fread(image,1,(size_t)bytes,disk)==(size_t)bytes);
    struct nas_ramdisk rd={0};
    struct nas_lwext4_device device={0};
    check(nas_ramdisk_attach(&rd,"symlink-image",image,(uint64_t)bytes,
                             512,create),"attach real NaSeed RAM BlockDevice");
    check(nas_lwext4_device_attach(&device,"symlink-image","symlink-ext4"),
          "register lwext4 device");
    check(ext4_mount("symlink-ext4","/mnt/",!create),"mount ext4");

    if(create) {
        check(ext4_dir_mk("/mnt/certs"),"create cert directory");
        ext4_fsblk_t poisoned[POISON_BLOCKS];
        assert(device.ext.fs);
        poison_and_remove(device.ext.fs,poisoned);
        create_all(device.ext.fs,poisoned,true);
        /* Recreating both inline and block-backed links catches stale
         * i_blocks/i_flags, wrong directory-entry types and inode reuse. */
        for(size_t i=0;i<sizeof(lengths)/sizeof(lengths[0]);i++) {
            char path[128];
            path_for(lengths[i],path);
            check(ext4_fremove(path),"unlink symlink");
        }
        create_all(device.ext.fs,poisoned,false);
    }
    verify_all();
    check(ext4_umount("/mnt/"),"unmount ext4");
    check(nas_lwext4_device_detach(&device,"symlink-ext4"),"detach lwext4");
    check(nas_ramdisk_detach(&rd),"detach RAM BlockDevice");
    if(create) {
        rewind(disk);
        assert(fwrite(image,1,(size_t)bytes,disk)==(size_t)bytes);
        assert(!fflush(disk));
    }
    assert(!fclose(disk));
    free(image);
    puts(create ? "[SYMLINK CREATE/RECREATE PASS] real physical bytes saved" :
                  "[SYMLINK INDEPENDENT REMOUNT PASS] all targets intact");
    return 0;
}
