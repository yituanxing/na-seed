/* One genuine freestanding Linux userspace binary source for every ISA.
 * Numbers/register ABI live in per-target assembly; no kernel test hooks.
 * Writes operate on an explicitly ephemeral Limine ext4 RAM image. */
#include <stdint.h>
enum { OP_OPENAT, OP_READ, OP_WRITE, OP_FSYNC, OP_LSEEK, OP_CLOSE, OP_EXIT };
extern long guest_syscall(long op,long a0,long a1,long a2,long a3);
static const char path[]="/mnt/sandbox/cross-isa-created";
static const char payload[]="NaSeed one portable Linux ELF for x86 RV64 AArch64 ext4 RW\n";
static const char passed[]="[PASS] common VFS ext4 RAM RW native Linux ELF\n";
static const char failed[]="[EXT4 COMMON RW DIAG] stage=";
static char result[256];
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
    stage=12;
    if(guest_syscall(OP_WRITE,1,(long)(uintptr_t)passed,sizeof(passed)-1,0)!=(long)(sizeof(passed)-1))goto fail;
    guest_syscall(OP_EXIT,0,0,0,0);
    for(;;){}
fail:
    char digits[3];
    digits[0]=(char)('0'+stage/10);
    digits[1]=(char)('0'+stage%10);
    digits[2]='\n';
    guest_syscall(OP_WRITE,1,(long)(uintptr_t)failed,sizeof(failed)-1,0);
    guest_syscall(OP_WRITE,1,(long)(uintptr_t)digits,sizeof(digits),0);
    guest_syscall(OP_EXIT,1,0,0,0);
    for(;;){}
}
