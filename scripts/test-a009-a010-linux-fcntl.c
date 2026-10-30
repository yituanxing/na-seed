/* One unchanged Linux C source, executed on the Linux host and as genuine
 * static native ELF on NaSeed x86-64/RV64/AArch64. Neither path substitutes
 * a mock VFS implementation or infers success from syscall availability. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#if defined(__aarch64__)
# define NATIVE_LINUX_LARGEFILE_BIT 0400000
#else
# define NATIVE_LINUX_LARGEFILE_BIT 0x8000
#endif

int main(void) {
    const char *path="/tmp/a009-a010-positional-fcntl";
    (void)unlink(path); /* Also makes repeated real guest boots independent. */
    int fd=open(path,O_CREAT|O_EXCL|O_TRUNC|O_RDWR|O_CLOEXEC,0600);
    assert(fd>=0);
    int status=fcntl(fd,F_GETFL);
    assert(status>=0 && (status&O_ACCMODE)==O_RDWR);
    assert((status&NATIVE_LINUX_LARGEFILE_BIT)!=0);
    assert(!(status&(O_CREAT|O_EXCL|O_TRUNC|O_CLOEXEC)));
    assert((fcntl(fd,F_GETFD)&FD_CLOEXEC)!=0);

    assert(write(fd,"AB",2)==2);
    assert(lseek(fd,1,SEEK_SET)==1);
    assert(fcntl(fd,F_SETFL,O_APPEND|O_NONBLOCK)==0);
    status=fcntl(fd,F_GETFL);
    assert((status&(O_APPEND|O_NONBLOCK))==(O_APPEND|O_NONBLOCK));
    int second=open(path,O_WRONLY|O_APPEND);
    assert(second>=0 && write(second,"C",1)==1);
    assert(close(second)==0);
    /* Linux pwrite+O_APPEND intentionally differs from POSIX. The other
     * open description changed the inode since fd was originally opened. */
    assert(pwrite(fd,"D",1,0)==1);
    assert(lseek(fd,0,SEEK_CUR)==1);
    char contents[5]={0};
    assert(pread(fd,contents,4,0)==4);
    assert(strcmp(contents,"ABCD")==0);

    int alias=dup(fd);
    assert(alias>=0);
    /* F_SETFL may alter O_APPEND/O_NONBLOCK, never the access mode or the
     * immutable O_DIRECTORY/O_NOFOLLOW bits of an existing description. */
    assert(fcntl(alias,F_SETFL,O_DIRECTORY|O_NOFOLLOW)==0);
    status=fcntl(fd,F_GETFL);
    assert((status&O_ACCMODE)==O_RDWR);
    assert(!(status&(O_APPEND|O_NONBLOCK|O_DIRECTORY|O_NOFOLLOW)));
    assert((status&NATIVE_LINUX_LARGEFILE_BIT)!=0);
    assert(pwrite(fd,"Z",1,0)==1);
    assert(lseek(fd,0,SEEK_CUR)==1);
    assert(pread(alias,contents,4,0)==4);
    assert(strcmp(contents,"ZBCD")==0);
    assert(close(alias)==0 && close(fd)==0);
    assert(unlink(path)==0);
    puts("A009_LINUX_PWRITE_APPEND_POSITION_PASS");

    int dir=open("/tmp",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK);
    assert(dir>=0);
    status=fcntl(dir,F_GETFL);
    assert(status>=0 && (status&O_ACCMODE)==O_RDONLY);
    assert((status&(O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK))==
           (O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK));
    assert((status&NATIVE_LINUX_LARGEFILE_BIT)!=0);
    assert(fcntl(dir,F_SETFL,0)==0);
    status=fcntl(dir,F_GETFL);
    assert((status&(O_DIRECTORY|O_NOFOLLOW))==
           (O_DIRECTORY|O_NOFOLLOW));
    assert(!(status&O_NONBLOCK));
    assert(close(dir)==0);

    int pathfd=open("/tmp",O_PATH|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    assert(pathfd>=0);
    status=fcntl(pathfd,F_GETFL);
    assert((status&(O_PATH|O_DIRECTORY|O_NOFOLLOW))==
           (O_PATH|O_DIRECTORY|O_NOFOLLOW));
    assert(!(status&(O_CLOEXEC|NATIVE_LINUX_LARGEFILE_BIT)));
    assert((fcntl(pathfd,F_GETFD)&FD_CLOEXEC)!=0);
    errno=0;
    assert(fcntl(pathfd,F_SETFL,O_NONBLOCK)==-1 && errno==EBADF);
    assert(close(pathfd)==0);
    puts("A010_LINUX_NATIVE_FCNTL_FLAGS_PASS");
    return 0;
}
