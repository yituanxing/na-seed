/* One unprivileged Linux ELF and one source for x86-64, RV64, AArch64.
 * No ramfs syscall shortcuts: run through shared NaSeed File/VFS/namei,
 * against the actual unmodified standalone jkent/ramfs memory backend. */
#include <stdint.h>
#include "../kernel/errno.h"
#include "../kernel/arch/linux_fcntl_abi.h"
/* ABI index matches the existing ext4-rw-syscall.S on all three targets;
 * only UNLINKAT was appended without disturbing existing syscall IDs. */
enum { OP_OPENAT, OP_READ, OP_WRITE, OP_FSYNC, OP_LSEEK, OP_CLOSE,
       OP_EXIT, OP_GETDENTS64, OP_MKDIRAT, OP_RENAMEAT, OP_FCHDIR,
       OP_GETCWD, OP_SYMLINKAT, OP_UNLINKAT };
extern long guest_syscall(long op,long a,long b,long c,long d);
static const char old_path[]="/ramfs/native-source";
static const char moved_path[]="/ramfs/native-renamed";
static const char parent_path[]="/ramfs/held";
static const char moved_parent[]="/ramfs/relocated";
static const char new_child[]="child";
static const char child_path[]="/ramfs/relocated/child";
static const char hidden_path[]="/ramfs/.nas_ramfs_orphans";
static const char old_data[]="first-vfs-inode";
static const char new_data[]="different-inode";
static const char child_data[]="dirfd-stays-on-original-inode";
static const char pass[]="[PASS] RAMFS portable native Linux syscalls: open-unlink/orphan/rename/dirfd/getdents\n";
static const char error[]="[RAMFS NATIVE DIAG] stage=";
static unsigned char buffer[1024];
static char cwd[256];
static int same(const unsigned char *a,const char *b,unsigned n) {
    for(unsigned i=0;i<n;i++)if(a[i]!=(unsigned char)b[i])return 0;
    return 1;
}
static int visible_name(const unsigned char *records,long count,const char *name) {
    long offset=0;
    while(offset<count) {
        if(count-offset<24)return -1;
        unsigned rec_len=(unsigned)records[offset+16]|
                         ((unsigned)records[offset+17]<<8);
        if(rec_len<24 || rec_len>(unsigned)(count-offset))return -1;
        unsigned i=0;
        while(name[i] && i<rec_len-19 &&
              records[offset+19+i]==(unsigned char)name[i])i++;
        if(!name[i] && i<rec_len-19 &&
           records[offset+19+i]==0)return 1;
        offset+=rec_len;
    }
    return 0;
}
__attribute__((noreturn)) void guest_main(void) {
    unsigned stage=1;
    long writer=guest_syscall(OP_OPENAT,-100,(long)(uintptr_t)old_path,0xc2,0640);
    if(writer<0)goto failed;
    stage=2;
    long reader=guest_syscall(OP_OPENAT,-100,(long)(uintptr_t)old_path,0,0);
    if(reader<0)goto failed;
    stage=3;
    if(guest_syscall(OP_WRITE,writer,(long)(uintptr_t)old_data,sizeof(old_data)-1,0)
       !=(long)(sizeof(old_data)-1))goto failed;
    stage=4;
    if(guest_syscall(OP_LSEEK,reader,0,2,0)!=(long)(sizeof(old_data)-1))
        goto failed;
    if(guest_syscall(OP_LSEEK,reader,0,0,0)!=0)goto failed;
    stage=5;
    if(guest_syscall(OP_READ,reader,(long)(uintptr_t)buffer,sizeof(old_data)-1,0)
       !=(long)(sizeof(old_data)-1)||
       !same(buffer,old_data,sizeof(old_data)-1))goto failed;
    stage=6;
    if(guest_syscall(OP_OPENAT,-100,(long)(uintptr_t)old_path,0xc2,0600)
       !=-EEXIST)goto failed;
    stage=7;
    long root=guest_syscall(OP_OPENAT,-100,(long)(uintptr_t)"/ramfs",
                            NAS_LINUX_NATIVE_O_DIRECTORY,0);
    if(root<0)goto failed;
    stage=8;
    long dent=guest_syscall(OP_GETDENTS64,root,(long)(uintptr_t)buffer,
                            sizeof(buffer),0);
    if(dent<=0 || visible_name(buffer,dent,"native-source")!=1 ||
       visible_name(buffer,dent,".nas_ramfs_orphans")!=0)goto failed;
    stage=9;
    if(guest_syscall(OP_OPENAT,-100,(long)(uintptr_t)hidden_path,0,0)!=-ENOENT)
        goto failed;
    stage=10;
    if(guest_syscall(OP_RENAMEAT,-100,(long)(uintptr_t)old_path,
                     -100,(long)(uintptr_t)moved_path)!=0)goto failed;
    if(guest_syscall(OP_OPENAT,-100,(long)(uintptr_t)old_path,0,0)!=-ENOENT)
        goto failed;
    stage=11;
    if(guest_syscall(OP_LSEEK,reader,0,0,0)!=0)goto failed;
    if(guest_syscall(OP_READ,reader,(long)(uintptr_t)buffer,sizeof(old_data)-1,0)
       !=(long)(sizeof(old_data)-1)||
       !same(buffer,old_data,sizeof(old_data)-1))goto failed;
    stage=12;
    if(guest_syscall(OP_UNLINKAT,-100,(long)(uintptr_t)moved_path,0,0)!=0)
        goto failed;
    if(guest_syscall(OP_OPENAT,-100,(long)(uintptr_t)moved_path,0,0)!=-ENOENT)
        goto failed;
    stage=13;
    if(guest_syscall(OP_LSEEK,reader,0,0,0)!=0)goto failed;
    if(guest_syscall(OP_READ,reader,(long)(uintptr_t)buffer,sizeof(old_data)-1,0)
       !=(long)(sizeof(old_data)-1)||
       !same(buffer,old_data,sizeof(old_data)-1))goto failed;
    stage=14;
    if(guest_syscall(OP_FSYNC,writer,0,0,0)!=0)goto failed;
    stage=15;
    long other=guest_syscall(OP_OPENAT,-100,(long)(uintptr_t)moved_path,0xc2,0600);
    if(other<0)goto failed;
    if(guest_syscall(OP_WRITE,other,(long)(uintptr_t)new_data,sizeof(new_data)-1,0)
       !=(long)(sizeof(new_data)-1))goto failed;
    stage=16;
    if(guest_syscall(OP_LSEEK,reader,0,0,0)!=0)goto failed;
    if(guest_syscall(OP_READ,reader,(long)(uintptr_t)buffer,sizeof(old_data)-1,0)
       !=(long)(sizeof(old_data)-1)||
       !same(buffer,old_data,sizeof(old_data)-1))goto failed;
    stage=17;
    if(guest_syscall(OP_CLOSE,other,0,0,0)!=0 ||
       guest_syscall(OP_CLOSE,writer,0,0,0)!=0 ||
       guest_syscall(OP_CLOSE,reader,0,0,0)!=0)goto failed;
    stage=18;
    other=guest_syscall(OP_OPENAT,-100,(long)(uintptr_t)moved_path,0,0);
    if(other<0)goto failed;
    if(guest_syscall(OP_READ,other,(long)(uintptr_t)buffer,sizeof(new_data)-1,0)
       !=(long)(sizeof(new_data)-1)||
       !same(buffer,new_data,sizeof(new_data)-1))goto failed;
    if(guest_syscall(OP_CLOSE,other,0,0,0)!=0)goto failed;
    stage=19;
    if(guest_syscall(OP_MKDIRAT,-100,(long)(uintptr_t)parent_path,0700,0)!=0)
        goto failed;
    stage=20;
    long dirfd=guest_syscall(OP_OPENAT,-100,(long)(uintptr_t)parent_path,
                             NAS_LINUX_NATIVE_O_DIRECTORY,0);
    if(dirfd<0)goto failed;
    stage=21;
    if(guest_syscall(OP_RENAMEAT,-100,(long)(uintptr_t)parent_path,
                     -100,(long)(uintptr_t)moved_parent)!=0)goto failed;
    stage=22;
    if(guest_syscall(OP_MKDIRAT,-100,(long)(uintptr_t)parent_path,0700,0)!=0)
        goto failed;
    stage=23;
    long child=guest_syscall(OP_OPENAT,dirfd,(long)(uintptr_t)new_child,0x42,0600);
    if(child<0)goto failed;
    if(guest_syscall(OP_WRITE,child,(long)(uintptr_t)child_data,
                     sizeof(child_data)-1,0)!=(long)(sizeof(child_data)-1))
        goto failed;
    if(guest_syscall(OP_CLOSE,child,0,0,0)!=0)goto failed;
    stage=24;
    child=guest_syscall(OP_OPENAT,-100,(long)(uintptr_t)child_path,0,0);
    if(child<0)goto failed;
    if(guest_syscall(OP_READ,child,(long)(uintptr_t)buffer,
                     sizeof(child_data)-1,0)!=(long)(sizeof(child_data)-1)||
       !same(buffer,child_data,sizeof(child_data)-1))goto failed;
    if(guest_syscall(OP_CLOSE,child,0,0,0)!=0)goto failed;
    stage=25;
    if(guest_syscall(OP_FCHDIR,dirfd,0,0,0)!=0)goto failed;
    long len=guest_syscall(OP_GETCWD,(long)(uintptr_t)cwd,sizeof(cwd),0,0);
    if(len!=(long)sizeof(moved_parent))goto failed;
    for(unsigned i=0;i<sizeof(moved_parent);i++)
        if(cwd[i]!=moved_parent[i])goto failed;
    stage=26;
    if(guest_syscall(OP_FCHDIR,root,0,0,0)!=0)goto failed;
    stage=27;
    if(guest_syscall(OP_UNLINKAT,dirfd,(long)(uintptr_t)new_child,0,0)!=0)
        goto failed;
    stage=28;
    if(guest_syscall(OP_UNLINKAT,-100,(long)(uintptr_t)moved_parent,0x200,0)!=0)
        goto failed;
    stage=29;
    if(guest_syscall(OP_GETCWD,(long)(uintptr_t)cwd,sizeof(cwd),0,0)
       !=(long)sizeof("/ramfs"))goto failed;
    stage=30;
    if(guest_syscall(OP_UNLINKAT,-100,(long)(uintptr_t)parent_path,0x200,0)!=0)
        goto failed;
    if(guest_syscall(OP_UNLINKAT,-100,(long)(uintptr_t)moved_path,0,0)!=0)
        goto failed;
    if(guest_syscall(OP_CLOSE,dirfd,0,0,0)!=0 ||
       guest_syscall(OP_CLOSE,root,0,0,0)!=0)goto failed;
    stage=31;
    if(guest_syscall(OP_WRITE,1,(long)(uintptr_t)pass,sizeof(pass)-1,0)
       !=(long)(sizeof(pass)-1))goto failed;
    guest_syscall(OP_EXIT,0,0,0,0);
    for(;;){}
failed:
    ;
    char digits[4]={(char)('0'+stage/100),
                    (char)('0'+(stage/10)%10),(char)('0'+stage%10),'\n'};
    guest_syscall(OP_WRITE,1,(long)(uintptr_t)error,sizeof(error)-1,0);
    guest_syscall(OP_WRITE,1,(long)(uintptr_t)digits,4,0);
    guest_syscall(OP_EXIT,1,0,0,0);
    for(;;){}
}
