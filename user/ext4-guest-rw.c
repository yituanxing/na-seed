/* One genuine freestanding Linux userspace binary source for every ISA.
 * Numbers/register ABI live in per-target assembly; no kernel test hooks.
 * Writes operate on an explicitly ephemeral Limine ext4 RAM image. */
#include <stdint.h>
#include "../kernel/arch/linux_fcntl_abi.h"
enum { OP_OPENAT, OP_READ, OP_WRITE, OP_FSYNC, OP_LSEEK, OP_CLOSE,
       OP_EXIT, OP_GETDENTS64, OP_MKDIRAT, OP_RENAMEAT, OP_FCHDIR, OP_GETCWD };
extern long guest_syscall(long op,long a0,long a1,long a2,long a3);
static const char path[]="/mnt/sandbox/cross-isa-created";
static const char payload[]="NaSeed one portable Linux ELF for x86 RV64 AArch64 ext4 RW\n";
static const char passed[]="[PASS] common VFS ext4 RAM RW native Linux ELF\n";
static const char failed[]="[EXT4 COMMON RW DIAG] stage=";
static char result[256];
static char cwd[256];
static const char original_dir[]="/mnt/sandbox/naseed-dirfd-original";
static const char new_dir[]="/mnt/sandbox/naseed-dirfd-renamed";
static const char relative_child[]="child";
static const char moved_child[]="/mnt/sandbox/naseed-dirfd-renamed/child";
static const char child_payload[]="inode-not-recycled";
static const char dirfd_pass[]="[PASS] VFS native renamed dirfd openat/fchdir/getcwd inode identity\n";
__attribute__((noreturn)) void guest_main(void) {
    unsigned stage=1;
    long writer=guest_syscall(OP_OPENAT,-100,(long)(uintptr_t)path,0x242,0644);
    if(writer<0)goto fail;
    stage=2;
    /* Open a distinct reader BEFORE growth; its old dentry snapshot must
     * not hide writer updates after the VFS inode-size lease is published. */
    long reader=guest_syscall(OP_OPENAT,-100,(long)(uintptr_t)path,0,0);
    if(reader<0)goto fail;
    stage=3;
    const long size=(long)(sizeof(payload)-1);
    if(guest_syscall(OP_WRITE,writer,(long)(uintptr_t)payload,size,0)!=size)goto fail;
    stage=4;
    if(guest_syscall(OP_FSYNC,writer,0,0,0)!=0)goto fail;
    stage=5;
    if(guest_syscall(OP_READ,reader,(long)(uintptr_t)result,size,0)!=size)goto fail;
    stage=6;
    for(long i=0;i<size;i++)if(result[i]!=payload[i])goto fail;
    stage=7;
    if(guest_syscall(OP_LSEEK,writer,0,0,0)!=0)goto fail;
    stage=8;
    if(guest_syscall(OP_READ,writer,(long)(uintptr_t)result,size,0)!=size)goto fail;
    for(long i=0;i<size;i++)if(result[i]!=payload[i])goto fail;
    stage=9;
    if(guest_syscall(OP_CLOSE,reader,0,0,0)!=0)goto fail;
    if(guest_syscall(OP_CLOSE,writer,0,0,0)!=0)goto fail;
    stage=10;
    long reopened=guest_syscall(OP_OPENAT,-100,(long)(uintptr_t)path,0,0);
    if(reopened<0)goto fail;
    stage=11;
    if(guest_syscall(OP_READ,reopened,(long)(uintptr_t)result,size,0)!=size)goto fail;
    for(long i=0;i<size;i++)if(result[i]!=payload[i])goto fail;
    if(guest_syscall(OP_CLOSE,reopened,0,0,0)!=0)goto fail;
    /* Linux real-guest control variable: directory FD owns the inode,
     * independent of its original dentry string. Rename it, recycle the
     * old name with a different inode, and ensure subsequent relative
     * openat and fchdir use the ORIGINAL directory, not the recycled one.
     * This same source runs x86-64, RV64, AArch64, RAM and VirtIO ext4. */
    stage=12;
    if(guest_syscall(OP_MKDIRAT,-100,(long)(uintptr_t)original_dir,
                     0700,0)!=0)goto fail;
    stage=13;
    long dirfd=guest_syscall(OP_OPENAT,-100,(long)(uintptr_t)original_dir,
                            NAS_LINUX_NATIVE_O_DIRECTORY,0);
    if(dirfd<0)goto fail;
    stage=14;
    if(guest_syscall(OP_RENAMEAT,-100,(long)(uintptr_t)original_dir,
                     -100,(long)(uintptr_t)new_dir)!=0)goto fail;
    stage=15;
    if(guest_syscall(OP_MKDIRAT,-100,(long)(uintptr_t)original_dir,
                     0700,0)!=0)goto fail;
    stage=16;
    long child=guest_syscall(OP_OPENAT,dirfd,(long)(uintptr_t)relative_child,
                            0x42,0600); /* O_CREAT|O_RDWR */
    if(child<0)goto fail;
    stage=17;
    if(guest_syscall(OP_WRITE,child,(long)(uintptr_t)child_payload,
                     sizeof(child_payload)-1,0)!=(long)(sizeof(child_payload)-1))
        goto fail;
    stage=18;
    if(guest_syscall(OP_FSYNC,child,0,0,0)!=0)goto fail;
    if(guest_syscall(OP_CLOSE,child,0,0,0)!=0)goto fail;
    stage=19;
    child=guest_syscall(OP_OPENAT,-100,(long)(uintptr_t)moved_child,0,0);
    if(child<0)goto fail;
    stage=20;
    if(guest_syscall(OP_READ,child,(long)(uintptr_t)result,
                     sizeof(child_payload)-1,0)!=(long)(sizeof(child_payload)-1))
        goto fail;
    for(unsigned i=0;i<sizeof(child_payload)-1;i++)
        if(result[i]!=child_payload[i])goto fail;
    if(guest_syscall(OP_CLOSE,child,0,0,0)!=0)goto fail;
    stage=21;
    if(guest_syscall(OP_FCHDIR,dirfd,0,0,0)!=0)goto fail;
    if(guest_syscall(OP_CLOSE,dirfd,0,0,0)!=0)goto fail;
    stage=22;
    long cwd_bytes=guest_syscall(OP_GETCWD,(long)(uintptr_t)cwd,sizeof(cwd),0,0);
    if(cwd_bytes!=(long)sizeof(new_dir))goto fail;
    for(unsigned i=0;i<sizeof(new_dir);i++)
        if(cwd[i]!=new_dir[i])goto fail;
    stage=23;
    if(guest_syscall(OP_WRITE,1,(long)(uintptr_t)dirfd_pass,
                     sizeof(dirfd_pass)-1,0)!=(long)(sizeof(dirfd_pass)-1))goto fail;
    stage=24;
    if(guest_syscall(OP_WRITE,1,(long)(uintptr_t)passed,sizeof(passed)-1,0)!=(long)(sizeof(passed)-1))goto fail;
    guest_syscall(OP_EXIT,0,0,0,0);
    for(;;){}
fail:
    ; /* C11 requires a statement immediately after a label. */
    char digits[3];
    digits[0]=(char)('0'+stage/10);
    digits[1]=(char)('0'+stage%10);
    digits[2]='\n';
    guest_syscall(OP_WRITE,1,(long)(uintptr_t)failed,sizeof(failed)-1,0);
    guest_syscall(OP_WRITE,1,(long)(uintptr_t)digits,sizeof(digits),0);
    guest_syscall(OP_EXIT,1,0,0,0);
    for(;;){}
}
