/* Shared real native ring3 Linux ELF tests the SAME second VFS provider on 3 ISAs. */
#include <stdint.h>
#include "../kernel/arch/linux_fcntl_abi.h"
enum {OPENAT,READ,WRITE,FSYNC,LSEEK,CLOSE,EXIT,GETDENTS64};
extern long guest_syscall(long,long,long,long,long);
static const char filename[]="/flash/source/program";
static const char dirname[]="/flash/source";
static const char expected[]="Second independent real filesystem through NaSeed VFS";
static const char success[]="[PASS] LITTLEFS portable native ring3 Linux ELF via VFS\n";
static const char failure[]="[LITTLEFS NATIVE DIAG] stage=";
static unsigned char buffer[512],directory[512];
__attribute__((noreturn)) void guest_main(void) {
    unsigned stage=1;
    long fd=guest_syscall(OPENAT,-100,(long)(uintptr_t)filename,0,0);
    if(fd<0)goto fail;
    stage=2;
    long n=(long)(sizeof(expected)-1);
    if(guest_syscall(READ,fd,(long)(uintptr_t)buffer,n,0)!=n)goto fail;
    for(long i=0;i<n;i++)if(buffer[i]!=(unsigned char)expected[i])goto fail;
    stage=3;
    if(guest_syscall(CLOSE,fd,0,0,0))goto fail;
    stage=4;
    fd=guest_syscall(OPENAT,-100,(long)(uintptr_t)dirname,
                     NAS_LINUX_NATIVE_O_DIRECTORY,0);
    if(fd<0)goto fail;
    stage=5;
    n=guest_syscall(GETDENTS64,fd,(long)(uintptr_t)directory,512,0);
    if(n<=0||n>512)goto fail;
    stage=6;
    long cursor=0;int found=0;
    while(cursor<n) {
        if(n-cursor<24)goto fail;
        unsigned len=(unsigned)directory[cursor+16]|
                     ((unsigned)directory[cursor+17]<<8);
        if(len<24 || len>(unsigned)(n-cursor))goto fail;
        if(len>=32 && directory[cursor+19]=='p' &&
           directory[cursor+20]=='r' && directory[cursor+21]=='o' &&
           directory[cursor+22]=='g' && directory[cursor+23]=='r' &&
           directory[cursor+24]=='a' && directory[cursor+25]=='m' &&
           directory[cursor+26]==0)found=1;
        cursor+=len;
    }
    if(!found)goto fail;
    stage=7;
    if(guest_syscall(CLOSE,fd,0,0,0))goto fail;
    stage=8;
    if(guest_syscall(WRITE,1,(long)(uintptr_t)success,sizeof(success)-1,0)
       !=(long)(sizeof(success)-1))goto fail;
    guest_syscall(EXIT,0,0,0,0);
    for(;;){}
fail:
    ;
    char digits[3]={(char)('0'+stage/10),(char)('0'+stage%10),'\n'};
    guest_syscall(WRITE,1,(long)(uintptr_t)failure,sizeof(failure)-1,0);
    guest_syscall(WRITE,1,(long)(uintptr_t)digits,3,0);
    guest_syscall(EXIT,1,0,0,0);
    for(;;){}
}
